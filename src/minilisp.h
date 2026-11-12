// This software is under the MIT License.

#ifndef _MINILISP_H_
#define _MINILISP_H_

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>


//======================================================================
// Lisp objects
//======================================================================

// The Lisp object type
enum {
    // Regular objects visible from the user
    TINT = 1,
    TCELL,
    TSYMBOL,
    TPRIMITIVE,
    TFUNCTION,
    TMACRO,
    TENV,
    // The marker that indicates the object has been moved to other location by GC. The new location
    // can be found at the forwarding pointer. Only the functions to do garbage collection set and
    // handle the object of this type. Other functions will never see the object of this type.
    TMOVED,
    TSTRING,
    TFLOAT,
    TVECTOR,
    // Const objects. They are statically allocated and will never be managed by GC.
    TTRUE,
    TNIL,
    TDOT,
    TCPAREN,
    TCBRACKET,
};

// Typedef for the primitive function
struct Obj;
typedef struct Obj *Primitive(void *root, struct Obj **env, struct Obj **args);

// The object type
typedef struct Obj {
    // The header of an object is 8 bytes long, so that a cons cell takes 24 bytes and an integer 16
    // (instead of 32 and 24 with a header made of three ints plus padding).

    // The type of the object. Any code that handles object needs to check its type first, then
    // access the following union members.
    uint8_t type;

    // The Lisp line where the object was created. 24 bits are enough for the line numbers of
    // the sources a Lisp program is made of; bigger numbers are truncated.
    unsigned int line_num : 24;

    // The total size of the object, including the header, the contents, and the padding at the
    // end of the object. It is always a multiple of the alignment (8 bytes).
    uint32_t size;

    // Object values.
    union {
        // Int
        long long value;
        // Cell
        struct {
            struct Obj *car;
            struct Obj *cdr;
        };
        // Vector: the number of elements, followed by the elements. The size of the object
        // does not tell the length, since it is rounded up.
        struct {
            long long len;
            struct Obj *elems[];
        };
        // Float
        double fvalue;
        // String
        char name[1];
        // Symbol: the binding of the symbol in the global environment, i.e. the pair
        // (symbol . value), or NULL if the symbol is not defined globally. Keeping it in the
        // symbol makes the lookup of global variables and functions independent of their number.
        struct {
            struct Obj *bind;
            char sym_name[];
        };
        // Primitive
        Primitive *fn;
        // Function or Macro
        struct {
            struct Obj *params;
            struct Obj *body;
            struct Obj *env;
        };
        // Environment frame. This is a linked list of association lists
        // containing the mapping from symbols to their value.
        struct {
            struct Obj *vars;
            struct Obj *up;
        };
        // Forwarding pointer
        void *moved;
    };
} Obj;

// The header must not grow: the memory consumption of every object depends on it.
typedef char obj_header_must_be_8_bytes[offsetof(Obj, value) == 8 ? 1 : -1];

typedef struct {
    char *filename;
    size_t file_len;
    int line_num;
} filepos_t;

// Reports an error and jumps back to the end of the current top-level evaluation.
void error(const char *fmt, int line_num, ...) __attribute__((noreturn, format(printf, 1, 3)));

void init_minilisp(Obj **env);

// Sets the text the reader will consume. The text must stay valid until it is fully evaluated.
void set_input(const char *text, size_t len);

// Evaluates all the expressions of the current input. Errors are reported on stderr.
int eval_input(void *root, Obj **env, Obj **expr);

// Loads and evaluates a file.
void process_file(void *root, char *fname, Obj **env, Obj **expr);

#endif // _MINILISP_H_
