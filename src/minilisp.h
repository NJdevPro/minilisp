#ifndef _MINILISP_H_
#define _MINILISP_H_

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
    // Const objects. They are statically allocated and will never be managed by GC.
    TTRUE,
    TNIL,
    TDOT,
    TCPAREN,
};

// Typedef for the primitive function
struct Obj;
typedef struct Obj *Primitive(void *root, struct Obj **env, struct Obj **args);

// The object type
typedef struct Obj {
    // The first word of the object represents the type of the object. Any code that handles object
    // needs to check its type first, then access the following union members.
    int type;

    // The total size of the object, including "type" field, this field, the contents, and the
    // padding at the end of the object.
    unsigned int size;

    int line_num;  // The Lisp line where object was created

    // Object values.
    union {
        // Int
        long long value;
        // Cell
        struct {
            struct Obj *car;
            struct Obj *cdr;
        };
        // Symbol
        char name[1];
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

typedef struct {
    char *filename;
    size_t file_len;
    int line_num;
} filepos_t;

void error(char *fmt, int line_num, ...);

void init_minilisp(Obj **env);
int eval_input(void *input, Obj **env, Obj **expr);

#endif // _MINILISP_H_
