#ifndef _MEMORY_H_
#define _MEMORY_H_

#include <stddef.h>
#include <stdbool.h>
#include "minilisp.h"

//======================================================================
// Memory management
//======================================================================

// The size of the heap in byte
#define MEMORY_SIZE 65536 * 4

extern void *gc_root;    // root of memory

// Currently we are using Cheney's copying GC algorithm, with which the available memory is split
// into two halves and all objects are moved from one half to another every time GC is invoked. That
// means the address of the object keeps changing. If you take the address of an object and keep it
// in a C variable, dereferencing it could cause SEGV because the address becomes invalid after GC
// runs.
//
// In order to deal with that, all access from C to Lisp objects will go through two levels of
// pointer dereferences. The C local variable is pointing to a pointer on the C stack, and the
// pointer is pointing to the Lisp object. GC is aware of the pointers in the stack and updates
// their contents with the objects' new addresses when GC happens.
//
// The following is a macro to reserve the area in the C stack for the pointers. The contents of
// this area are considered to be GC root.
//
// Be careful not to bypass the two levels of pointer indirections. If you create a direct pointer
// to an object, it'll cause a subtle bug. Such code would work in most cases but fails with SEGV if
// GC happens during the execution of the code. Any code that allocates memory may invoke GC.

#define ROOT_END ((void *)-1)

static inline void** add_root_frame(void **prev_frame, int size, void *frame[]) {
    frame[0] = prev_frame;
    for (int i = 1; i <= size; i++)
        frame[i] = NULL;
    frame[size + 1] = ROOT_END;
    return frame;
}

#define DEFINE1(prev_frame, var1)                \
    void *root_frame[3];                         \
    prev_frame = add_root_frame(prev_frame, 1, root_frame);       \
    Obj **var1 = (Obj **)(root_frame + 1);

#define DEFINE2(prev_frame, var1, var2)          \
    void *root_frame[4];                         \
    prev_frame = add_root_frame(prev_frame, 2, root_frame);  \
    Obj **var1 = (Obj **)(root_frame + 1);       \
    Obj **var2 = (Obj **)(root_frame + 2);

#define DEFINE3(prev_frame, var1, var2, var3)   \
    void *root_frame[5];                        \
    prev_frame = add_root_frame(prev_frame, 3, root_frame); \
    Obj **var1 = (Obj **)(root_frame + 1);      \
    Obj **var2 = (Obj **)(root_frame + 2);      \
    Obj **var3 = (Obj **)(root_frame + 3);

#define DEFINE4(prev_frame, var1, var2, var3, var4)         \
    void *root_frame[6];                        \
    prev_frame = add_root_frame(prev_frame, 4, root_frame); \
    Obj **var1 = (Obj **)(root_frame + 1);      \
    Obj **var2 = (Obj **)(root_frame + 2);      \
    Obj **var3 = (Obj **)(root_frame + 3);      \
    Obj **var4 = (Obj **)(root_frame + 4);


void *alloc_semispace();
Obj *alloc(void *root, int type, size_t size);
void gc(void *root);

#endif