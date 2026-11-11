// This software is in the public domain.

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <setjmp.h>
#include <string.h>
#include <sys/resource.h>
#include <unistd.h>
#include "minilisp.h"
#include "gc.h"

jmp_buf context;

extern filepos_t filepos;

void error(const char *fmt, int line_num, ...) {
    va_list ap;
    va_start(ap, line_num);
    fprintf(stderr, "%s[%d]: ", filepos.filename, line_num);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    // Jump right back to the end of eval 
    longjmp(context, 1);
}

// Constants
static Obj *True = &(Obj){ .type = TTRUE, .size = sizeof(Obj) };
static Obj *Nil = &(Obj){ .type = TNIL, .size = sizeof(Obj) };
static Obj *Dot = &(Obj){ .type = TDOT, .size = sizeof(Obj) };
static Obj *Cparen = &(Obj){ .type = TCPAREN, .size = sizeof(Obj) };

//======================================================================
// Constructors
//======================================================================

// The list containing all symbols. Such data structure is traditionally called the "obarray", but I
// avoid using it as a variable name as this is not an array but a list.
extern Obj *Symbols;

void *gc_root = NULL;    // root of memory

// Small integers are preallocated: they are never managed by GC (like the constants above) and
// computing with them does not allocate.
#define SMALL_INT_MIN (-128)
#define SMALL_INT_MAX 1023
static Obj small_ints[SMALL_INT_MAX - SMALL_INT_MIN + 1];

static void init_small_ints(void) {
    for (int i = 0; i < SMALL_INT_MAX - SMALL_INT_MIN + 1; i++)
        small_ints[i] = (Obj){ .type = TINT, .size = sizeof(Obj), .value = i + SMALL_INT_MIN };
}

static Obj *make_int(void *root, long long value) {
    if (SMALL_INT_MIN <= value && value <= SMALL_INT_MAX)
        return &small_ints[value - SMALL_INT_MIN];
    Obj *r = alloc(root, TINT, sizeof(long long));
    r->value = value;
    r->line_num = filepos.line_num;
    return r;
}

static Obj *cons(void *root, Obj **car, Obj **cdr) {
    Obj *cell = alloc(root, TCELL, sizeof(Obj *) * 2);
    cell->car = *car;
    cell->cdr = *cdr;
    cell->line_num = filepos.line_num;
    return cell;
}

static Obj *make_symbol(void *root, const char *name) {
    Obj *sym = alloc(root, TSYMBOL, sizeof(Obj *) + strlen(name) + 1);
    sym->bind = NULL;
    strcpy(sym->sym_name, name);
    sym->line_num = filepos.line_num;
    return sym;
}

static Obj *make_primitive(void *root, Primitive *fn) {
    Obj *r = alloc(root, TPRIMITIVE, sizeof(Primitive *));
    r->fn = fn;
    r->line_num = filepos.line_num;
    return r;
}

static Obj *make_function(void *root, Obj **env, int type, Obj **params, Obj **body) {
    assert(type == TFUNCTION || type == TMACRO);
    Obj *r = alloc(root, type, sizeof(Obj *) * 3);
    r->line_num = filepos.line_num;
    r->params = *params;
    r->body = *body;
    r->env = *env;
    return r;
}

struct Obj *make_env(void *root, Obj **vars, Obj **up) {
    Obj *r = alloc(root, TENV, sizeof(Obj *) * 2);
    r->vars = *vars;
    r->up = *up;
    return r;
}

static Obj *make_string(void *root, const char *str) {
    size_t len = strlen(str);
    Obj *r = alloc(root, TSTRING, len + 1);
    r->line_num = filepos.line_num;
    strcpy(r->name, str);  // We can reuse the name field for string data
    return r;
}

// Returns ((x . y) . a)
static Obj *acons(void *root, Obj **x, Obj **y, Obj **a) {
    DEFINE1(root, cell);
    *cell = cons(root, x, y);
    return cons(root, cell, a);
}

//======================================================================
// Parser
//
// This is a hand-written recursive-descendent parser.
//======================================================================

#define SYMBOL_MAX_LEN 200
static const char symbol_chars[] = "~!@#$%^&*-_=+:/?<>";

static Obj *read_expr(void *root);

// The text being read. Reading directly from a buffer is much faster than going through stdio,
// and it does not require to hijack stdin.
typedef struct {
    const char *text;
    size_t len;
    size_t pos;
} reader_t;

static reader_t reader;

void set_input(const char *text, size_t len) {
    reader = (reader_t){ text, len, 0 };
}

static inline int peek(void) {
    return reader.pos < reader.len ? (unsigned char)reader.text[reader.pos] : EOF;
}

static int read_char(void) {
    int c = peek();
    if (c == EOF)
        return EOF;
    reader.pos++;
    if (c == '\n') {
        filepos.line_num++;
        if (peek() == '\r')
            reader.pos++;
    } else if (c == '\r') {
        filepos.line_num++;
        if (peek() == '\n')
            reader.pos++;
    }
    return c;
}

static bool is_symbol_char(int c) {
    return c > 0 && strchr(symbol_chars, c) != NULL;
}

// The C stack is the limit of the recursion depth. Fail with a Lisp error before reaching it,
// instead of crashing with SEGV.
static uintptr_t stack_limit = 0;

static void init_stack_limit(void) {
    char here;
    struct rlimit rl;
    size_t size = 8 << 20;
    if (getrlimit(RLIMIT_STACK, &rl) == 0 && rl.rlim_cur != RLIM_INFINITY)
        size = rl.rlim_cur;
    // Keep a safety margin for the C library and the error handling.
    stack_limit = (uintptr_t)&here - (size - (size >> 3) - (64 << 10));
}

static inline void check_stack(int line_num) {
    char here;
    if ((uintptr_t)&here < stack_limit)
        error("Stack overflow: the recursion is too deep", line_num);
}

// Destructively reverses the given list.
static Obj *reverse(Obj *p) {
    Obj *ret = Nil;
    while (p != Nil) {
        Obj *head = p;
        p = p->cdr;
        head->cdr = ret;
        ret = head;
    }
    return ret;
}

// Skips the input until newline is found. Newline is one of \r, \r\n or \n.
static void skip_line(void) {
    int c;
    // read_char() counts the lines and swallows the second character of \r\n and \n\r.
    while ((c = read_char()) != EOF && c != '\n' && c != '\r')
        ;
}

// Reads a list. Note that '(' has already been read.
static Obj *read_list(void *root) {
    check_stack(filepos.line_num);
    DEFINE3(root, obj, head, last);
    *head = Nil;
    for (;;) {
        *obj = read_expr(root);
        if (!*obj)
            error("Unclosed parenthesis", filepos.line_num);
        if (*obj == Cparen)
            return reverse(*head);
        if (*obj == Dot) {
            if (*head == Nil)
                error("Unexpected dot at the beginning of a list", filepos.line_num);
            *last = read_expr(root);
            if (!*last || *last == Cparen || *last == Dot)
                error("Expression expected after dot", filepos.line_num);
            if (read_expr(root) != Cparen)
                error("Closed parenthesis expected after dot", filepos.line_num);
            Obj *ret = reverse(*head);
            (*head)->cdr = *last;
            return ret;
        }
        *head = cons(root, obj, head);
    }
}

// May create a new symbol. If there's a symbol with the same name, it will not create a new symbol
// but return the existing one.
static Obj *intern(void *root, const char *name) {
    for (Obj *p = Symbols; p != Nil; p = p->cdr)
        if (strcmp(name, p->car->sym_name) == 0)
            return p->car;
    DEFINE1(root, sym);
    *sym = make_symbol(root, name);
    Symbols = cons(root, sym, &Symbols);
    return *sym;
}

// Reader marcro ' (single quote). It reads an expression and returns (quote <expr>).
static Obj *read_quote(void *root) {
    DEFINE2(root, sym, tmp);
    *sym = intern(root, "quote");
    *tmp = read_expr(root);
    *tmp = cons(root, tmp, &Nil);
    *tmp = cons(root, sym, tmp);
    return *tmp;
}

static long long read_number(long long val) {
    while (isdigit(peek())) {
        int digit = read_char() - '0';
        if (val > (LLONG_MAX - digit) / 10)
            error("Integer too large", filepos.line_num);
        val = val * 10 + digit;
    }
    return val;
}

static Obj *read_symbol(void *root, char c) {
    char buf[SYMBOL_MAX_LEN + 1];
    buf[0] = c;
    int len = 1;
    while (isalnum(peek()) || is_symbol_char(peek())) {
        if (SYMBOL_MAX_LEN <= len)
            error("Symbol name too long", filepos.line_num);
        buf[len++] = read_char();
    }
    buf[len] = '\0';
    return intern(root, buf);
}

static Obj *read_string(void *root) {
    char buf[1024];
    size_t i = 0;
    
    while (1) {
        int c = read_char();
        if (c == EOF)
            error("Unclosed string literal", filepos.line_num);
        if (c == '"')
            break;
        if (c == '\\') {
            c = read_char();
            if (c == 'n') c = '\n';
            else if (c == 't') c = '\t';
            else if (c == 'r') c = '\r';
        }
        if (i >= sizeof(buf) - 1)
            error("String too long", filepos.line_num);
        buf[i++] = c;
    }
    buf[i] = '\0';
    return make_string(root, buf);
}

static Obj *read_expr(void *root) {
    for (;;) {
        int c = read_char();
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        if (c == ';') {
            skip_line();
            continue;
        }
        if (c == '(')
            return read_list(root);
        if (c == ')')
            return Cparen;
        if (c == '.')
            return Dot;
        if (c == '\'')
            return read_quote(root);
        if (c == '"')
            return read_string(root);
        if (isdigit(c))
            return make_int(root, read_number(c - '0'));
        if (c == '-' && isdigit(peek()))
            return make_int(root, -read_number(0));
        if (isalpha(c) || is_symbol_char(c))
            return read_symbol(root, c);
        if (c == EOF)
            return NULL;

        error("Don't know how to handle %c", filepos.line_num, c);
    }
}

// Prints the given object.
static void print(Obj *obj) {
    switch (obj->type) {
    case TCELL:
        fputc('(', stdout);
        for (;;) {
            print(obj->car);
            if (obj->cdr == Nil)
                break;
            if (obj->cdr->type != TCELL) {
                fputs(" . ", stdout);
                print(obj->cdr);
                break;
            }
            fputc(' ', stdout);
            obj = obj->cdr;
        }
        fputc(')', stdout);
        break;

    case TINT   : printf("%lld", obj->value);
        break;
    case TSYMBOL: fputs(obj->sym_name, stdout);
        break;
    case TPRIMITIVE: fputs("<primitive>", stdout);
        break;
    case TFUNCTION: fputs("<function>", stdout);
        break;
    case TMACRO : fputs("<macro>", stdout);
        break;
    case TMOVED : fputs("<moved>", stdout);
        break;
    case TTRUE  : fputc('t', stdout);
        break;
    case TNIL   : fputs("()", stdout);
        break;
    case TSTRING:
        for (char *p = obj->name; *p; p++) {
            if (*p == '"') fputs("\\\"", stdout);
            else if (*p == '\n') fputc('\n', stdout);
            else if (*p == '\t') fputc('\t', stdout);
            else if (*p == '\r') fputc('\r', stdout);
            else fputc(*p, stdout);
        }
        break;
    default:
        error("Bug: print: Unknown tag type: %d", obj->line_num, obj->type);
    }
    //puts("");
}

// Returns the length of the given list. -1 if it's not a proper list.
static int length(Obj *list) {
    int len = 0;
    for (; list->type == TCELL; list = list->cdr)
        len++;
    return list == Nil ? len : -1;
}

//======================================================================
// Evaluator
//======================================================================

static Obj *eval(void *root, Obj **env, Obj **obj);

// The global environment is the frame at the top of the chain of frames, the only one which has no
// parent. Its variables are not in the frame itself, they are stored in the symbols.
static inline bool is_global(Obj *env) {
    return env->up == Nil;
}

static void add_variable(void *root, Obj **env, Obj **sym, Obj **val) {
    DEFINE2(root, vars, tmp);
    if (is_global(*env)) {
        // Redefining a variable just changes its value: the symbol is the only way to reach it.
        if ((*sym)->bind) {
            (*sym)->bind->cdr = *val;
        } else {
            *tmp = cons(root, sym, val);
            (*sym)->bind = *tmp;
        }
        return;
    }
    *vars = (*env)->vars;
    *tmp = acons(root, sym, val, vars);
    (*env)->vars = *tmp;
}

// Returns a newly created environment frame.
static Obj *push_env(void *root, Obj **env, Obj **vars, Obj **vals) {
    DEFINE3(root, map, sym, val);
    *map = Nil;
    for (; (*vars)->type == TCELL; *vars = (*vars)->cdr, *vals = (*vals)->cdr) {
        if ((*vals)->type != TCELL)
            error("Cannot apply function: number of argument does not match",
            (*vars)->line_num);
        *sym = (*vars)->car;
        *val = (*vals)->car;
        *map = acons(root, sym, val, map);
    }
    if (*vars != Nil)
        *map = acons(root, vars, vals, map);
    else if (*vals != Nil)
        error("Cannot apply function: number of argument does not match", filepos.line_num);
    return make_env(root, map, env);
}

// Evaluates the list elements from head and returns the last return value.
static Obj *progn(void *root, Obj **env, Obj **list) {
    DEFINE2(root, lp, r);
    *r = Nil;
    for (*lp = *list; *lp != Nil; *lp = (*lp)->cdr) {
        *r = (*lp)->car;
        *r = eval(root, env, r);
    }
    return *r;
}

// Evaluates all the list elements and returns their return values as a new list.
static Obj *eval_list(void *root, Obj **env, Obj **list) {
    DEFINE4(root, head, lp, expr, result);
    *head = Nil;
    // Walk with our own slot: the caller's list must be left untouched.
    for (*lp = *list; (*lp)->type == TCELL; *lp = (*lp)->cdr) {
        *expr = (*lp)->car;
        *result = eval(root, env, expr);
        *head = cons(root, result, head);
    }
    if (*lp != Nil)
        error("Improper list of arguments", (*lp)->line_num);
    return reverse(*head);
}

static bool is_list(Obj *obj) {
    return obj == Nil || obj->type == TCELL;
}

// Returns the new environment frame in which the body of the function fn is evaluated: the
// parameters are bound to the (already evaluated) arguments. The args slot is modified.
static Obj *bind_args(void *root, Obj **fn, Obj **args) {
    DEFINE2(root, params, newenv);
    *params = (*fn)->params;
    *newenv = (*fn)->env;
    return push_env(root, newenv, params, args);
}

// Calls the function or macro fn with the (already evaluated, for a function) arguments, and
// returns the value of its body. This is not a tail call: used for the macros expansion.
static Obj *apply_func(void *root, Obj **env, Obj **fn, Obj **args) {
    DEFINE2(root, newenv, body);
    *newenv = bind_args(root, fn, args);
    *body = (*fn)->body;
    return progn(root, newenv, body);
}

// Searches for a variable by symbol. Returns null if not found.
static Obj *find(Obj **env, Obj *sym) {
    // Search the local environments first...
    Obj *p = *env;
    for (; !is_global(p); p = p->up) {
        for (Obj *cell = p->vars; cell != Nil; cell = cell->cdr) {
            Obj *bind = cell->car;
            if (sym == bind->car)
                return bind;
        }
    }
    // ...then the global one, which is stored in the symbol.
    return sym->bind;
}

// Expands the given macro application form.
static Obj *macroexpand(void *root, Obj **env, Obj **obj) {
    if ((*obj)->type != TCELL || (*obj)->car->type != TSYMBOL)
        return *obj;
    DEFINE3(root, bind, macro, args);
    *bind = find(env, (*obj)->car);
    if (!*bind || (*bind)->cdr->type != TMACRO)
        return *obj;
    *macro = (*bind)->cdr;
    *args = (*obj)->cdr;
    return apply_func(root, env, macro, args);
}

// The special forms which are evaluated by eval itself, in order to handle the tail calls.
static Obj *prim_if(void *root, Obj **env, Obj **list);
static Obj *prim_progn(void *root, Obj **env, Obj **list);

// Evaluates the objects which are not function application forms. Returns NULL for a form.
static inline Obj *eval_simple(Obj **env, Obj *obj) {
    switch (obj->type) {
    case TINT:
    case TPRIMITIVE:
    case TFUNCTION:
    case TTRUE:
    case TSTRING:
    case TNIL:
        // Self-evaluating objects
        return obj;
    case TSYMBOL: {
        // Variable
        Obj *bind = find(env, obj);
        if (!bind)
            error("Undefined symbol: %s", obj->line_num, obj->sym_name);
        return bind->cdr;
    }
    case TCELL:
        return NULL;
    default:
        error("Bug: eval: Unknown tag type: %d", obj->line_num, obj->type);
    }
}

// Evaluates a function application form, or anything else that comes in tail position.
//
// The tail calls do not consume stack: the expression in tail position of an "if", of a "progn",
// of the body of a function or of the expansion of a macro is not evaluated by a recursive call.
// Instead, it becomes the current expression and the loop goes on. For that, the current
// expression and environment live in slots owned by this function. The slots of the caller must
// not be modified, because the caller keeps using them.
static Obj *eval_form(void *root, Obj **env, Obj **obj) {
    DEFINE5(root, cur, cenv, fn, args, tmp);
    *cur = *obj;
    *cenv = *env;
    for (;;) {
        Obj *r = eval_simple(cenv, *cur);
        if (r)
            return r;

        // Function application form
        *args = (*cur)->cdr;
        if ((*cur)->car->type == TSYMBOL) {
            // Look the head symbol up only once: it is either a macro to expand or the function
            // to call.
            Obj *bind = find(cenv, (*cur)->car);
            if (!bind)
                error("Undefined symbol: %s", (*cur)->car->line_num, (*cur)->car->sym_name);
            *fn = bind->cdr;
        } else {
            *fn = (*cur)->car;
            *fn = eval(root, cenv, fn);
        }

        if ((*fn)->type == TMACRO) {
            // Expand, then evaluate the expansion in place of the form
            *cur = apply_func(root, cenv, fn, args);
            continue;
        }
        if ((*fn)->type != TPRIMITIVE && (*fn)->type != TFUNCTION)
            error("The head of a list must be a function",
                  (*fn)->line_num ? (*fn)->line_num : (*cur)->line_num);
        if (!is_list(*args))
            error("argument must be a list", (*args)->line_num);

        if ((*fn)->type == TFUNCTION) {
            // Evaluate the arguments, then the body in a new environment
            *tmp = eval_list(root, cenv, args);
            *cenv = bind_args(root, fn, tmp);
            *args = (*fn)->body;
            goto sequence;
        }

        if ((*fn)->fn == prim_if) {
            // (if cond then else ...)
            if (length(*args) < 2)
                error("Malformed if", (*cur)->line_num);
            *tmp = (*args)->car;
            *tmp = eval(root, cenv, tmp);
            if (*tmp != Nil) {
                *cur = (*args)->cdr->car;
                continue;
            }
            *args = (*args)->cdr->cdr;
            goto sequence;
        }
        if ((*fn)->fn == prim_progn)
            goto sequence;

        // The other primitives are not evaluating anything in tail position
        return (*fn)->fn(root, cenv, args);

    sequence:
        // Evaluates the expressions of the list *args. The last one is in tail position.
        if (*args == Nil)
            return Nil;
        while ((*args)->type == TCELL && (*args)->cdr != Nil) {
            *tmp = (*args)->car;
            eval(root, cenv, tmp);
            *args = (*args)->cdr;
        }
        if ((*args)->type != TCELL)
            error("Improper list of expressions", (*args)->line_num);
        *cur = (*args)->car;
    }
}

// Evaluates the S expression.
static Obj *eval(void *root, Obj **env, Obj **obj) {
    check_stack((*obj)->line_num);
    Obj *r = eval_simple(env, *obj);
    return r ? r : eval_form(root, env, obj);
}

//======================================================================
// Primitive functions and special forms
//======================================================================

// (list expr ...)
static Obj *prim_list(void *root, Obj **env, Obj **list) {
    DEFINE1(root, values);
    *values = eval_list(root, env, list);  // Evaluate all arguments
    return *values;                         // Return them as a list
}

// 'expr
static Obj *prim_quote(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("Malformed quote", (*list)->line_num);
    return (*list)->car;
}

static Obj *prim_atom(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("atom takes only 1 argument", (*list)->line_num);
    Obj *values = eval_list(root, env, list);
    return (values->car->type != TCELL) ? True : Nil;
}

// (cons expr expr)
static Obj *prim_cons(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2)
        error("Malformed cons", (*list)->line_num);
    Obj *cell = eval_list(root, env, list);
    cell->cdr = cell->cdr->car;
    return cell;
}

// (car <cell>)
static Obj *prim_car(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (args == Nil || args->car->type != TCELL || args->cdr != Nil)
        error("Malformed car", (*list)->line_num);
    return args->car->car;
}

// (cdr <cell>)
static Obj *prim_cdr(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (args == Nil || args->car->type != TCELL || args->cdr != Nil)
        error("Malformed cdr", (*list)->line_num);
    return args->car->cdr;
}

// (setq <symbol> expr)
static Obj *prim_setq(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2 || (*list)->car->type != TSYMBOL)
        error("Malformed setq", (*list)->line_num);
    DEFINE2(root, bind, value);
    *bind = find(env, (*list)->car);
    if (!*bind)
        error("Unbound variable %s", (*list)->line_num, (*list)->car->sym_name);
    *value = (*list)->cdr->car;
    *value = eval(root, env, value);
    (*bind)->cdr = *value;
    return *value;
}

// (setcar <cell> expr)
static Obj *prim_setcar(void *root, Obj **env, Obj **list) {
    DEFINE1(root, args);
    *args = eval_list(root, env, list);
    if (length(*args) != 2 || (*args)->car->type != TCELL)
        error("Malformed setcar", (*list)->line_num);
    (*args)->car->car = (*args)->cdr->car;
    return (*args)->car;
}

// (while cond expr ...)
static Obj *prim_while(void *root, Obj **env, Obj **list) {
    if (length(*list) < 2)
        error("Malformed while", (*list)->line_num);
    DEFINE2(root, cond, exprs);
    *cond = (*list)->car;
    while (eval(root, env, cond) != Nil) {
        *exprs = (*list)->cdr;
        progn(root, env, exprs);
    }
    return Nil;
}

// (gensym)
static Obj *prim_gensym(void *root, Obj **env, Obj **list) {
  static unsigned long count = 0;
  char buf[32];
  snprintf(buf, sizeof(buf), "G__%lu", count++);
  return make_symbol(root, buf);
}

// (length <cell> | length <string> | length ...)
static Obj *prim_length(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    int len = length(args);
    if (len == 1) {
        Obj *car = args->car;
        if (car != Nil) { 
            if (car->type == TSTRING) {
                len = strlen(car->name);
            }
            else if (car->type == TCELL) {
                for (len = 0; car != Nil && car->type == TCELL; car = car->cdr) 
                    len++;
            }
            else {
                error("When length has a single argument, it must be a list or a string", 
                (*list)->line_num);
            }
        }
    }

    return make_int(root, len);
}

// Returns a new list containing the elements of *list in the reverse order. *list is left untouched.
static Obj *reverse_copy(void *root, Obj **list) {
    DEFINE3(root, p, head, elem);
    *head = Nil;
    for (*p = *list; (*p)->type == TCELL; *p = (*p)->cdr) {
        *elem = (*p)->car;
        *head = cons(root, elem, head);
    }
    return *head;
}

// (reverse ... | reverse <cell> | reverse <string>)
// The arguments are never modified: a new list or string is returned.
static Obj *prim_reverse(void *root, Obj **env, Obj **list) {
    DEFINE1(root, args);
    *args = eval_list(root, env, list);
    if (length(*args) != 1)
        return reverse(*args);  // The list of evaluated arguments is ours: reverse it in place
    *args = (*args)->car;
    if (*args == Nil)
        return Nil;
    if ((*args)->type == TCELL)
        return reverse_copy(root, args);
    if ((*args)->type == TSTRING) {
        size_t len = strlen((*args)->name);
        Obj *r = alloc(root, TSTRING, len + 1);
        r->line_num = filepos.line_num;
        const char *src = (*args)->name;  // Read after alloc, which may have moved the string
        for (size_t i = 0; i < len; i++)
            r->name[i] = src[len - 1 - i];
        r->name[len] = '\0';
        return r;
    }
    error("When reverse has a single argument, it must be a list or a string",
          (*list)->line_num);
}

// (op <integer> ...) with op one of + - * / %
// Arithmetic wraps around on overflow, like the unsigned arithmetic does.
static Obj *arith(void *root, Obj **env, Obj **list, char op) {
    Obj *args = eval_list(root, env, list);
    for (Obj *p = args; p != Nil; p = p->cdr)
        if (p->car->type != TINT)
            error("%c takes only numbers", (*list)->line_num, op);
    if (args == Nil) {
        if (op == '+') return make_int(root, 0);
        if (op == '*') return make_int(root, 1);
        error("%c takes at least 1 number", (*list)->line_num, op);
    }
    unsigned long long r = args->car->value;
    if (op == '-' && args->cdr == Nil)
        return make_int(root, (long long)(0 - r));
    for (Obj *p = args->cdr; p != Nil; p = p->cdr) {
        long long v = p->car->value;
        switch (op) {
        case '+': r += v; break;
        case '-': r -= v; break;
        case '*': r *= v; break;
        default:  // '/' and '%'
            if (v == 0)
                error("Division by zero", (*list)->line_num);
            if (v == -1)  // Avoids the overflow of LLONG_MIN / -1
                r = op == '/' ? 0 - r : 0;
            else
                r = op == '/' ? (long long)r / v : (long long)r % v;
        }
    }
    return make_int(root, (long long)r);
}

// (+ <integer> ...)
static Obj *prim_plus(void *root, Obj **env, Obj **list)   { return arith(root, env, list, '+'); }
// (- <integer> ...)
static Obj *prim_minus(void *root, Obj **env, Obj **list)  { return arith(root, env, list, '-'); }
static Obj *prim_mult(void *root, Obj **env, Obj **list)   { return arith(root, env, list, '*'); }
static Obj *prim_div(void *root, Obj **env, Obj **list)    { return arith(root, env, list, '/'); }
static Obj *prim_modulo(void *root, Obj **env, Obj **list) { return arith(root, env, list, '%'); }

// (op <integer> <integer>)
#define PRIM_COMPARISON_OP(PRIM_OP, OP)                             \
static Obj *PRIM_OP(void *root, Obj **env, Obj **list) {            \
    Obj *args = eval_list(root, env, list);                         \
    if (length(args) != 2)                                          \
        error(#OP " takes only 2 number", (*list)->line_num);       \
    Obj *x = args->car;                                             \
    Obj *y = args->cdr->car;                                        \
    if (x->type != TINT || y->type != TINT)                         \
        error(#OP " takes only 2 numbers", (*list)->line_num);      \
    return x->value OP y->value ? True : Nil;                       \
}

PRIM_COMPARISON_OP(prim_num_eq, ==)
PRIM_COMPARISON_OP(prim_lt, <)
PRIM_COMPARISON_OP(prim_lte, <=)
PRIM_COMPARISON_OP(prim_gt, >)
PRIM_COMPARISON_OP(prim_gte, >=)

// (not <cell>)
static Obj *prim_not(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("not accepts 1 argument", (*list)->line_num);
    Obj *values = eval_list(root, env, list);
    return values->car == Nil ? True : Nil;
}

// Returns a newly allocated path for a file to load. A relative path is first looked up in the
// directory of the file being loaded, then in the current directory.
__attribute__((noinline)) static char *resolve_path(const char *name) {
    const char *current = filepos.filename;
    const char *slash = current ? strrchr(current, '/') : NULL;
    if (name[0] != '/' && slash) {
        size_t dirlen = slash - current + 1;
        char *path = malloc(dirlen + strlen(name) + 1);
        if (path) {
            memcpy(path, current, dirlen);
            strcpy(path + dirlen, name);
            if (access(path, R_OK) == 0)
                return path;
            free(path);
        }
    }
    return strdup(name);
}

static Obj *prim_load(void *root, Obj **env, Obj **list) {
    DEFINE1(root, expr);
    Obj *args = eval_list(root, env, list);
    if (length(args) != 1 || args->car->type != TSTRING)
        error("load: filename must be a string", (*list)->line_num);
    // The name must be copied: the string lives in the GC heap and would move.
    char *name = resolve_path(args->car->name);
    if (!name)
        error("Out of memory in load", (*list)->line_num);

    // Save the old context and set up a new one for error handling
    jmp_buf old_context;
    memcpy(&old_context, &context, sizeof(jmp_buf));
    filepos_t calling_file = filepos;
    reader_t calling_reader = reader;

    if (setjmp(context) == 0)
        process_file(root, name, env, expr);

    // Restore the old context, the position in the calling file and the calling text
    memcpy(&context, &old_context, sizeof(jmp_buf));
    filepos = calling_file;
    reader = calling_reader;
    free(name);
    return Nil;
}

static Obj *handle_function(void *root, Obj **env, Obj **list, int type) {
    if ((*list)->type != TCELL || !is_list((*list)->car) || (*list)->cdr->type != TCELL)
        error("Malformed lambda", (*list)->line_num);
    Obj *p = (*list)->car;
    for (; p->type == TCELL; p = p->cdr)
        if (p->car->type != TSYMBOL)
            error("Parameter must be a symbol", (*list)->line_num);
    if (p != Nil && p->type != TSYMBOL)
        error("Parameter must be a symbol", (*list)->line_num);
    DEFINE2(root, params, body);
    *params = (*list)->car;
    *body = (*list)->cdr;
    return make_function(root, env, type, params, body);
}

// (lambda (<symbol> ...) expr ...)
static Obj *prim_lambda(void *root, Obj **env, Obj **list) {
    return handle_function(root, env, list, TFUNCTION);
}

static Obj *handle_defun(void *root, Obj **env, Obj **list, int type) {
    if (length(*list) < 3 || (*list)->car->type != TSYMBOL || (*list)->cdr->type != TCELL)
        error("Malformed defun: correct form is (defun <symbol> (<symbol> ...) expr ...)"
        , (*list)->line_num);
    DEFINE3(root, fn, sym, rest);
    *sym = (*list)->car;
    *rest = (*list)->cdr;
    *fn = handle_function(root, env, rest, type);
    add_variable(root, env, sym, fn);
    return *fn;
}

// (defun <symbol> (<symbol> ...) expr ...)
static Obj *prim_defun(void *root, Obj **env, Obj **list) {
    return handle_defun(root, env, list, TFUNCTION);
}

// (define <symbol> expr)
static Obj *prim_define(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2 || (*list)->car->type != TSYMBOL)
        error("Malformed define", (*list)->line_num);
    DEFINE2(root, sym, value);
    *sym = (*list)->car;
    *value = (*list)->cdr->car;
    *value = eval(root, env, value);
    add_variable(root, env, sym, value);
    return *value;
}

// (defmacro <symbol> (<symbol> ...) expr ...)
static Obj *prim_defmacro(void *root, Obj **env, Obj **list) {
    return handle_defun(root, env, list, TMACRO);
}

// (macroexpand expr)
static Obj *prim_macroexpand(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("Malformed macroexpand", (*list)->line_num);
    DEFINE1(root, body);
    *body = (*list)->car;
    return macroexpand(root, env, body);
}

// (print expr ...)
// Evaluates and prints each argument, separated by a space.
static Obj *prim_print(void *root, Obj **env, Obj **list) {
    DEFINE2(root, lp, tmp);
    for (*lp = *list; (*lp)->type == TCELL; *lp = (*lp)->cdr) {
        if (*lp != *list)
            fputc(' ', stdout);
        *tmp = (*lp)->car;
        print(eval(root, env, tmp));
    }
    return Nil;
}

// (println expr ...)
static Obj *prim_println(void *root, Obj **env, Obj **list) {
    prim_print(root, env, list);
    fputc('\n', stdout);
    return Nil;
}

static Obj *prim_progn(void *root, Obj **env, Obj **list) {
    return progn(root, env, list);
}

// (if expr expr expr ...)
static Obj *prim_if(void *root, Obj **env, Obj **list) {
    if (length(*list) < 2)
        error("Malformed if", (*list)->line_num);
    DEFINE3(root, cond, then, els);
    *cond = (*list)->car;
    *cond = eval(root, env, cond);
    if (*cond != Nil) {
        *then = (*list)->cdr->car;
        return eval(root, env, then);
    }
    *els = (*list)->cdr->cdr;
    return *els == Nil ? Nil : progn(root, env, els);
}

// (eq expr expr)
static Obj *prim_eq(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2)
        error("eq takes 2 arguments only", (*list)->line_num);
    Obj *values = eval_list(root, env, list);
    Obj *first = values->car;
    Obj *second = values->cdr->car;
    if (first->type == TINT && second->type == TINT)
        return first->value == second->value ? True : Nil;
    if (first->type == TSTRING){
        if (second->type == TSTRING)
            return strcmp(first->name, second->name) == 0 ? True : Nil;
        else
            error("The 2 arguments of eq must be of the same type", (*list)->line_num);
    } 
    return first == second ? True : Nil;
}

// String primitives
static size_t int_to_str(char *buf, long long val) {
    return (size_t)snprintf(buf, 24, "%lld", val);
}

// (string-concat <string | integer> ...)
static Obj *prim_string_concat(void *root, Obj **env, Obj **list) {
    DEFINE1(root, args);
    *args = eval_list(root, env, list);
    char num[24];

    // First pass: calculate total length needed
    size_t total_len = 0;
    for (Obj *p = *args; p != Nil; p = p->cdr) {
        if (p->car->type == TINT)
            total_len += int_to_str(num, p->car->value);
        else if (p->car->type == TSTRING)
            total_len += strlen(p->car->name);
        else
            error("string-concat arguments must be strings or numbers", (*list)->line_num);
    }

    // The result is written directly in the new string. alloc may move the arguments, which
    // are reachable through the root.
    Obj *result = alloc(root, TSTRING, total_len + 1);
    result->line_num = filepos.line_num;

    // Second pass: copy all the strings
    char *out = result->name;
    for (Obj *p = *args; p != Nil; p = p->cdr) {
        if (p->car->type == TINT) {
            out += int_to_str(out, p->car->value);
        } else {
            size_t n = strlen(p->car->name);
            memcpy(out, p->car->name, n);
            out += n;
        }
    }
    *out = '\0';
    return result;
}

static Obj *prim_symbol_to_string(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (length(args) != 1)
        error("symbol->string requires 1 argument", (*list)->line_num);

    if (args->car->type != TSYMBOL)
        error("symbol->string argument must be a symbol", (*list)->line_num);

    // The name is copied out of the GC heap before allocating
    char name[SYMBOL_MAX_LEN + 1];
    strncpy(name, args->car->sym_name, sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';
    return make_string(root, name);
}

static Obj *prim_string_to_symbol(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (length(args) != 1)
        error("string->symbol requires 1 argument", (*list)->line_num);

    if (args->car->type != TSTRING)
        error("string->symbol argument must be a string", (*list)->line_num);
    if (strlen(args->car->name) > SYMBOL_MAX_LEN)
        error("Symbol name too long", (*list)->line_num);

    // The name is copied out of the GC heap before allocating
    char name[SYMBOL_MAX_LEN + 1];
    strcpy(name, args->car->name);
    return intern(root, name);
}

static Obj *prim_exit(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("exit accepts 1 argument", (*list)->line_num);
    Obj *values = eval_list(root, env, list);
    Obj *first = values->car; 
    if (first->type != TINT)
        error("exit takes an integer", (*list)->line_num);
    exit(first->value);
}

static void add_primitive(void *root, Obj **env, char *name, Primitive *fn) {
    DEFINE2(root, sym, prim);
    *sym = intern(root, name);
    *prim = make_primitive(root, fn);
    add_variable(root, env, sym, prim);
}

static void define_constants(void *root, Obj **env) {
    DEFINE1(root, sym);
    *sym = intern(root, "t");
    add_variable(root, env, sym, &True);
}

// Reads the whole file in a newly allocated buffer. Returns its length, or -1 on failure.
static long read_file(char *fname, char **text) {
    *text = NULL;
    FILE *f = fopen(fname, "rb");
    if (!f) {
        printf("Failed to load file %s\n", fname);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (length < 0) {
        printf("Failed to read file %s\n", fname);
        fclose(f);
        return -1;
    }

    *text = malloc(length + 1);
    if (!*text) {
        puts("Out of memory.");
        fclose(f);
        return -1;
    }

    if (fread(*text, 1, length, f) != (size_t)length) {
        printf("Failed to read entire file %s\n", fname);
        free(*text);
        *text = NULL;
        fclose(f);
        return -1;
    }

    (*text)[length] = '\0';
    fclose(f);
    return length;
}

// Evaluates all the expressions of a file. fname must stay valid until the function returns.
void process_file(void *root, char *fname, Obj **env, Obj **expr) {
    char *text = NULL;
    long len = read_file(fname, &text);
    if (len < 0) return;

    filepos_t calling_file = filepos;
    reader_t calling_reader = reader;
    set_input(text, len);
    filepos.filename = fname;
    filepos.file_len = len;
    filepos.line_num = 1;

    // eval_input stops at the first error: go on with the rest of the file
    while (reader.pos < reader.len)
        eval_input(root, env, expr);

    reader = calling_reader;
    filepos = calling_file;
    free(text);
}

static void define_primitives(void *root, Obj **env) {
    add_primitive(root, env, "list", prim_list);
    add_primitive(root, env, "quote", prim_quote);
    add_primitive(root, env, "cons", prim_cons);
    add_primitive(root, env, "car", prim_car);
    add_primitive(root, env, "cdr", prim_cdr);
    add_primitive(root, env, "setq", prim_setq);
    add_primitive(root, env, "setcar", prim_setcar);
    add_primitive(root, env, "while", prim_while);
    add_primitive(root, env, "gensym", prim_gensym);
    add_primitive(root, env, "not", prim_not);
    add_primitive(root, env, "+", prim_plus);
    add_primitive(root, env, "-", prim_minus);
    add_primitive(root, env, "*", prim_mult);
    add_primitive(root, env, "/", prim_div);
    add_primitive(root, env, "mod", prim_modulo);
    add_primitive(root, env, "length", prim_length);
    add_primitive(root, env, "reverse", prim_reverse);
    add_primitive(root, env, "<", prim_lt);
    add_primitive(root, env, ">", prim_gt);
    add_primitive(root, env, "<=", prim_lte);
    add_primitive(root, env, ">=", prim_gte);
    add_primitive(root, env, "define", prim_define);
    add_primitive(root, env, "defun", prim_defun);
    add_primitive(root, env, "defmacro", prim_defmacro);
    add_primitive(root, env, "macroexpand", prim_macroexpand);
    add_primitive(root, env, "lambda", prim_lambda);
    add_primitive(root, env, "atom", prim_atom);
    add_primitive(root, env, "if", prim_if);
    add_primitive(root, env, "progn", prim_progn);
    add_primitive(root, env, "=", prim_num_eq);
    add_primitive(root, env, "eq", prim_eq);
    add_primitive(root, env, "print", prim_print);
    add_primitive(root, env, "println", prim_println);
    add_primitive(root, env, "string-concat", prim_string_concat);
    add_primitive(root, env, "symbol->string", prim_symbol_to_string);
    add_primitive(root, env, "string->symbol", prim_string_to_symbol);
    add_primitive(root, env, "load", prim_load);
    add_primitive(root, env, "exit", prim_exit);
}

//======================================================================
// Entry point
//======================================================================

void init_minilisp(Obj **env) {
    // Memory allocation
    gc_init();
    init_small_ints();
    init_stack_limit();

    // Constants and primitives. env is a slot of the caller's root frame (gc_root), which must be
    // used as the root: the environment would not survive a GC otherwise.
    Symbols = Nil;
    *env = make_env(gc_root, &Nil, &Nil);
    define_constants(gc_root, env);
    define_primitives(gc_root, env);
}

int eval_input(void *root, Obj **env, Obj **expr) {
    if (setjmp(context) == 0) {
        while (true) {
            *expr = read_expr(root);         
            if (!*expr) 
                return 0;
            if (*expr == Cparen)
                error("Stray close parenthesis", filepos.line_num);
            if (*expr == Dot)
                error("Stray dot", filepos.line_num);
            print(eval(root, env, expr));
            putc('\n', stdout);
        }
    }
    return 0;
}
