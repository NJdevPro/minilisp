#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdint.h>
#include <sys/mman.h>
#include "gc.h"

extern filepos_t filepos;

// The pointer pointing to the beginning of the current heap (the semi-space we allocate from)
void *memory;

// The size in bytes of the current heap
size_t heap_size = MEMORY_SIZE;

// The unused semi-space. It is kept mapped between two collections, so that a collection does not
// have to ask the kernel for fresh pages every time. It becomes the to-space of the next GC.
static void *spare_space;
static size_t spare_size;

// The old heap while a collection is running
static void *from_space;
static size_t from_size;

// The number of bytes allocated from the heap
size_t mem_nused = 0;

// Flags to debug GC
bool gc_running = false;
bool debug_gc = false;
bool always_gc = false;

// The list containing all symbols. Such data structure is traditionally called the "obarray", but I
// avoid using it as a variable name as this is not an array but a list.
Obj *Symbols;

// Round up the given value to a multiple of size. Size must be a power of 2. It adds size - 1
// first, then zero-ing the least significant bits to make the result a multiple of size. I know
// these bit operations may look a little bit tricky, but it's efficient and thus frequently used.
static inline size_t roundup(size_t var, size_t size) {
    return (var + size - 1) & ~(size - 1);
}

// Returns true if the environment variable is defined and not the empty string.
static bool getEnvFlag(char *name) {
    char *val = getenv(name);
    return val && val[0];
}

static void *map_space(size_t size) {
    void *p = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    return p == MAP_FAILED ? NULL : p;
}

// Allocates the two semi-spaces and reads the debug flags. Must be called once at startup.
void gc_init(void) {
    debug_gc = getEnvFlag("MINILISP_DEBUG_GC");
    always_gc = getEnvFlag("MINILISP_ALWAYS_GC");
    heap_size = spare_size = MEMORY_SIZE;
    memory = map_space(heap_size);
    spare_space = map_space(spare_size);
    if (!memory || !spare_space) {
        fputs("Cannot allocate the heap\n", stderr);
        exit(1);
    }
    mem_nused = 0;
}

// Allocates memory block. This may start GC if we don't have enough memory.
Obj *alloc(void *root, int type, size_t size) {
    // The object must be large enough to contain a pointer for the forwarding pointer. Make it
    // larger if it's smaller than that.
    size = roundup(size, sizeof(void *));

    // Add the size of the type tag and size fields.
    size += offsetof(Obj, value);

    // Round up the object size to the nearest alignment boundary, so that the next object will be
    // allocated at the proper alignment boundary. Currently we align the object at the same
    // boundary as the pointer.
    size = roundup(size, sizeof(void *));

    // If the debug flag is on, run a GC at every allocation to force all the existing objects to
    // move to new addresses, and make the old heap inaccessible. By doing this the GC behavior
    // becomes more predictable and repeatable. If there's a memory bug that the C variable has a
    // direct reference to a Lisp object, the pointer will become invalid by this GC call.
    // Dereferencing that will immediately cause SEGV.
    // Otherwise, run GC only when the available memory is not large enough.
    if (always_gc || heap_size < mem_nused + size)
        gc(root);

    // The request is larger than what is left after GC: grow the heap.
    while (heap_size < mem_nused + size && heap_size < MAX_HEAP_SIZE)
        if (!gc_collect(root, heap_size * 2))
            break;

    // Give up if we couldn't satisfy the memory request. This can happen if the requested size
    // was too large or the heap was filled with too many live objects.
    if (heap_size < mem_nused + size)
        error("Memory exhausted", filepos.line_num);

    // Allocate the object. The heap is not necessarily zeroed (it can be a recycled semi-space).
    Obj *obj = (Obj *)((uint8_t *)memory + mem_nused);
    obj->type = type;
    obj->size = size;
    obj->line_num = 0;
    mem_nused += size;
    return obj;
}

//======================================================================
// Garbage collector
//======================================================================

// Cheney's algorithm uses two pointers to keep track of GC status. At first both pointers point to
// the beginning of the to-space. As GC progresses, they are moved towards the end of the
// to-space. The objects before "scan1" are the objects that are fully copied. The objects between
// "scan1" and "scan2" have already been copied, but may contain pointers to the from-space. "scan2"
// points to the beginning of the free space.
static Obj *scan1;
static Obj *scan2;

// Moves one object from the from-space to the to-space. Returns the object's new address. If the
// object has already been moved, does nothing but just returns the new address.
static inline Obj *forward(Obj *obj) {
    // If the object's address is not in the from-space, the object is not managed by GC nor it
    // has already been moved to the to-space.
    ptrdiff_t offset = (uint8_t *)obj - (uint8_t *)from_space;
    if (offset < 0 || (size_t)offset >= from_size)
        return obj;

    // The pointer is pointing to the from-space, but the object there was a tombstone. Follow the
    // forwarding pointer to find the new location of the object.
    if (obj->type == TMOVED)
        return obj->moved;

    // Otherwise, the object has not been moved yet. Move it.
    Obj *newloc = scan2;
    memcpy(newloc, obj, obj->size);
    scan2 = (Obj *)((uint8_t *)scan2 + obj->size);

    // Put a tombstone at the location where the object used to occupy, so that the following call
    // of forward() can find the object's new location.
    obj->type = TMOVED;
    obj->moved = newloc;
    return newloc;
}

// Copies the root objects.
static void forward_root_objects(void *root) {
    Symbols = forward(Symbols);
    for (void **frame = root; frame; frame = *(void ***)frame)
        for (int i = 1; frame[i] != ROOT_END; i++)
            if (frame[i])
                frame[i] = forward(frame[i]);
}

// Copies all the live objects to a to-space of new_size bytes, which becomes the heap. Returns
// false, leaving the heap untouched, if the new to-space could not be allocated.
bool gc_collect(void *root, size_t new_size) {
    assert(!gc_running);

    // Get the to-space: reuse the spare semi-space when it has the right size.
    void *to_space = spare_space;
    if (spare_size != new_size) {
        to_space = map_space(new_size);
        if (!to_space)
            return false;
        munmap(spare_space, spare_size);
    } else if (always_gc) {
        mprotect(to_space, new_size, PROT_READ | PROT_WRITE);
    }
    gc_running = true;

    from_space = memory;
    from_size = heap_size;
    memory = to_space;

    // Initialize the two pointers for GC. Initially they point to the beginning of the to-space.
    scan1 = scan2 = memory;

    // Copy the GC root objects first. This moves the pointer scan2.
    forward_root_objects(root);

    // Copy the objects referenced by the GC root objects located between scan1 and scan2. Once it's
    // finished, all live objects (i.e. objects reachable from the root) will have been copied to
    // the to-space.
    while (scan1 < scan2) {
        switch (scan1->type) {
        case TINT:
        case TFLOAT:
        case TPRIMITIVE:
        case TSTRING:
            // Any of the above types does not contain a pointer to a GC-managed object.
            break;
        case TSYMBOL:
            if (scan1->bind)
                scan1->bind = forward(scan1->bind);
            break;
        case TCELL:
            scan1->car = forward(scan1->car);
            scan1->cdr = forward(scan1->cdr);
            break;
        case TFUNCTION:
        case TMACRO:
            scan1->params = forward(scan1->params);
            scan1->body = forward(scan1->body);
            scan1->env = forward(scan1->env);
            break;
        case TENV:
            scan1->vars = forward(scan1->vars);
            scan1->up = forward(scan1->up);
            break;
        default:
            // The heap is corrupted: we cannot recover from this, nor unwind safely.
            fprintf(stderr, "Bug: copy: unknown type %d\n", scan1->type);
            abort();
        }
        scan1 = (Obj *)((uint8_t *)scan1 + scan1->size);
    }

    // Finish up GC: the old heap becomes the spare semi-space. In debug mode, make it inaccessible
    // so that any stale pointer to it crashes immediately.
    size_t old_nused = mem_nused;
    spare_space = from_space;
    spare_size = from_size;
    if (always_gc)
        mprotect(spare_space, spare_size, PROT_NONE);
    heap_size = new_size;
    mem_nused = (size_t)((uint8_t *)scan1 - (uint8_t *)memory);
    if (debug_gc)
        fprintf(stderr, "GC: %zu bytes out of %zu bytes copied.\n", mem_nused, old_nused);
    gc_running = false;
    return true;
}

// Implements Cheney's copying garbage collection algorithm.
// http://en.wikipedia.org/wiki/Cheney%27s_algorithm
// If more than half of the heap is still in use afterwards, the heap is doubled, otherwise we
// would collect again and again after every few allocations.
void gc(void *root) {
    gc_collect(root, heap_size);
    while (mem_nused > heap_size / 2 && heap_size < MAX_HEAP_SIZE)
        if (!gc_collect(root, heap_size * 2))
            break;
}
