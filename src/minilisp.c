// This software is in the public domain.

#include <assert.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#include <string.h>
#include "minilisp.h"
#include "gc.h"

jmp_buf context;

extern filepos_t filepos;

void error(char *fmt, int line_num, ...) {
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

extern Obj *alloc(void *root, int type, size_t size);

static Obj *make_int(void *root, long long value) {
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

static Obj *make_symbol(void *root, char *name) {
    Obj *sym = alloc(root, TSYMBOL, strlen(name) + 1);
    strcpy(sym->name, name);
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
const char symbol_chars[] = "~!@#$%^&*-_=+:/?<>";

void swap(char *left, char *right) {
    char tmp = *left;
    *left = *right;
    *right = tmp;
}

static Obj *read_expr(void *root);

static int peek(void) {
    int c = getchar();
    ungetc(c, stdin);
    return c;
}

static int read_char(void) {
    int c = getchar();
    if (c == '\n') {
        filepos.line_num++;
        if (peek() == '\r') {
            getchar();
        }
    } else if (c == '\r') {
        filepos.line_num++;
        if (peek() == '\n') {
            getchar();
        }
    }
    return c;
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
    for (;;) {
        char c = getchar();
        if (c == EOF || c == '\n'){
            filepos.line_num++;
            return;
        }
        if (c == '\r') {
            if (peek() == '\n')
                getchar();
            return;
        }
    }
}

// Reads a list. Note that '(' has already been read.
static Obj *read_list(void *root) {
    DEFINE3(root, obj, head, last);
    *head = Nil;
    for (;;) {
        *obj = read_expr(root);
        if (!*obj)
            error("Unclosed parenthesis", filepos.line_num);
        if (*obj == Cparen)
            return reverse(*head);
        if (*obj == Dot) {
            *last = read_expr(root);
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
static Obj *intern(void *root, char *name) {
    for (Obj *p = Symbols; p != Nil; p = p->cdr)
        if (strcmp(name, p->car->name) == 0)
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

static long long read_number(int val) {
    while (isdigit(peek()))
        val = val * 10 + (read_char() - '0');
    return val;
}

static Obj *read_symbol(void *root, char c) {
    char buf[SYMBOL_MAX_LEN + 1];
    buf[0] = c;
    int len = 1;
    while (isalnum(peek()) || strchr(symbol_chars, peek())) {
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
        char c = read_char();
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
        if (isalpha(c) || strchr(symbol_chars, c))
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
    case TSYMBOL: fputs(obj->name, stdout);
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

static void add_variable(void *root, Obj **env, Obj **sym, Obj **val) {
    DEFINE2(root, vars, tmp);
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
    return make_env(root, map, env);
}

// Evaluates the list elements from head and returns the last return value.
static Obj *progn(void *root, Obj **env, Obj **list) {
    DEFINE2(root, lp, r);
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
    for (lp = list; *lp != Nil; *lp = (*lp)->cdr) {
        *expr = (*lp)->car;
        *result = eval(root, env, expr);
        *head = cons(root, result, head);
    }
    return reverse(*head);
}

static bool is_list(Obj *obj) {
    return obj == Nil || obj->type == TCELL;
}

static Obj *apply_func(void *root, Obj **env, Obj **fn, Obj **args) {
    DEFINE3(root, params, newenv, body);
    *params = (*fn)->params;
    *newenv = (*fn)->env;
    *newenv = push_env(root, newenv, params, args);
    *body = (*fn)->body;
    return progn(root, newenv, body);
}

// Apply fn with args.
static Obj *apply(void *root, Obj **env, Obj **fn, Obj **args) {
    if (!is_list(*args))
        error("argument must be a list", (*args)->line_num);
    if ((*fn)->type == TPRIMITIVE)
        return (*fn)->fn(root, env, args);
    if ((*fn)->type == TFUNCTION) {
        DEFINE1(root, eargs);
        *eargs = eval_list(root, env, args);
        return apply_func(root, env, fn, eargs);
    }
    error("not supported", (*args)->line_num);
    return Nil; //fix warning
}

// Searches for a variable by symbol. Returns null if not found.
static Obj *find(Obj **env, Obj *sym) {
    for (Obj *p = *env; p != Nil; p = p->up) { // search all environments
        for (Obj *cell = p->vars; cell != Nil; cell = cell->cdr) {
            Obj *bind = cell->car;
            if (sym == bind->car)
                return bind;
        }
    }
    return NULL;
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

// Evaluates the S expression.
static Obj *eval(void *root, Obj **env, Obj **obj) {
    switch ((*obj)->type) {
    case TINT:
    case TPRIMITIVE:
    case TFUNCTION:
    case TTRUE:
    case TSTRING:
    case TNIL:
        // Self-evaluating objects
        return *obj;
    case TSYMBOL: {
        // Variable
        Obj *bind = find(env, *obj);
        if (!bind) {
            error("Undefined symbol: %s", (*obj)->line_num, (*obj)->name);
        }
        return bind->cdr;
    }
    case TCELL: {
        // Function application form
        DEFINE3(root, fn, expanded, args);
        *expanded = macroexpand(root, env, obj);
        if (*expanded != *obj)
            return eval(root, env, expanded);
        *fn = (*obj)->car;
        *fn = eval(root, env, fn);
        *args = (*obj)->cdr;
        if ((*fn)->type != TPRIMITIVE && (*fn)->type != TFUNCTION)
            error("The head of a list must be a function", (*obj)->line_num);
        return apply(root, env, fn, args);
    }
    default:
        error("Bug: eval: Unknown tag type: %d", (*obj)->line_num, (*obj)->type);
    }
    return Nil; // fix warning
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
        error("atom takes ontly 1 argument", (*list)->line_num);
    return ((*list)->car->type != TCELL) ? True : Nil;
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
    if (args->car->type != TCELL || args->cdr != Nil)
        error("Malformed car", (*list)->line_num);
    return args->car->car;
}

// (cdr <cell>)
static Obj *prim_cdr(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (args->car->type != TCELL || args->cdr != Nil)
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
        error("Unbound variable %s", (*list)->line_num, (*list)->car->name);
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
        eval_list(root, env, exprs);
    }
    return Nil;
}

// (gensym)
static Obj *prim_gensym(void *root, Obj **env, Obj **list) {
  static int count = 0;
  char buf[10];
  snprintf(buf, sizeof(buf), "G__%d", count++);
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

// (reverse ... | reverse <cell>)
static Obj *prim_reverse(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    int len = length(args);
    if (len != 1) {
        return reverse(args);
    }
    else { 
        Obj *car = args->car;
        if (car != Nil) { 
            if (car->type == TCELL) {
                return reverse(car);
            }
            else if(car->type == TSTRING){
                char *left = car->name, 
                     *right = left + strlen(car->name) - 1;
                while (left <= right) {
                    swap(left, right);
                    left++, right--;
                }
            }
            else {
                error("When reverse has a single argument, it must be a list", 
                (*list)->line_num);
            }
        }
        return car;
    }
}

#define PRIM_ARITHMETIC_OP(PRIM_OP, OP, OPEQ)                       \
static Obj *PRIM_OP(void *root, Obj **env, Obj **list) {            \
    Obj *args = eval_list(root, env, list);                         \
    long long r = args->car->value;                                 \
    for (Obj *p = args->cdr; p != Nil; p = p->cdr) {                \
        if (p->car->type != TINT)                                   \
            error(#OP " takes only numbers", (*list)->line_num);    \
        r OPEQ p->car->value;                                       \
    }                                                               \
    return make_int(root, r);                                       \
}

// (+ <integer> ...)
PRIM_ARITHMETIC_OP(prim_plus, +, += )
PRIM_ARITHMETIC_OP(prim_mult, *, *= )
PRIM_ARITHMETIC_OP(prim_div , /, /= )
PRIM_ARITHMETIC_OP(prim_modulo, %, %= )

// (- <integer> ...)
static Obj *prim_minus(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    for (Obj *p = args; p != Nil; p = p->cdr)
        if (p->car->type != TINT)
            error("- takes only numbers", (*list)->line_num);
    if (args->cdr == Nil)
        return make_int(root, -args->car->value);
    long long r = args->car->value;
    for (Obj *p = args->cdr; p != Nil; p = p->cdr)
        r -= p->car->value;
    return make_int(root, r);
}

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

extern void process_file(char *fname, Obj **env, Obj **expr);

static Obj *prim_load(void *root, Obj **env, Obj **list) {
    DEFINE1(root, expr);
    Obj *args = eval_list(root, env, list);
    if (args->car->type != TSTRING){
        error("load: filename must be a string", (*list)->line_num);
    }
    char *name = args->car->name;
    
    // Save old context and set up new one for error handling
    jmp_buf old_context;
    memcpy(&old_context, &context, sizeof(jmp_buf));
    
    if (setjmp(context) == 0) {
        filepos_t calling_file = filepos;
        process_file(name, env, expr);
        filepos = calling_file;
    }
    
    // Restore old context
    memcpy(&context, &old_context, sizeof(jmp_buf));
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

// (print ...)
static Obj *prim_print(void *root, Obj **env, Obj **list) {
    DEFINE1(root, tmp);
    *tmp = (*list)->car;
    print(eval(root, env, tmp));
    return Nil;
}


// (println ...)
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
    if (first->type == TSTRING){
        if (second->type == TSTRING)
            return strcmp(first->name, second->name) == 0 ? True : Nil;
        else
            error("The 2 arguments of eq must be of the same type", (*list)->line_num);
    } 
    return first == second ? True : Nil;
}

// String primitives
static Obj *prim_string_concat(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    
    // First pass: calculate total length needed
    size_t total_len = 1;  // Start with 1 for null terminator
    for (Obj *p = args; p != Nil; p = p->cdr) {
        if (p->car->type != TSTRING && p->car->type != TINT)
            error("string-concat arguments must be strings or numbers", 
            (*list)->line_num);
        if (p->car->type == TINT) {
            long long val = p->car->value;
            char var[22];
            snprintf(var, sizeof(var), "%lld", val);
            total_len += strlen(var);
        }
        else {
            total_len += strlen(p->car->name);
        }
    }
    
    char *buf = malloc(total_len);
    if (!buf)
        error("Out of memory in string-concat", (*list)->line_num);
    buf[0] = '\0';
    
    // Second pass: concatenate all strings
    for (Obj *p = args; p != Nil; p = p->cdr) {
        if (p->car->type == TINT) {
            long long val = p->car->value;
            char var[22];
            snprintf(var, sizeof(var), "%lld", val);
            strcat(buf, var);
        }
        else {
            strcat(buf, p->car->name);
        }
    }
    
    Obj *result = make_string(root, buf);
    free(buf);
    return result;
}

static Obj *prim_symbol_to_string(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (length(args) != 1)
        error("symbol->string requires 1 argument", (*list)->line_num);
    
    if (args->car->type != TSYMBOL)
        error("symbol->string argument must be a symbol", (*list)->line_num);
        
    return make_string(root, args->car->name);
}

static Obj *prim_string_to_symbol(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (length(args) != 1)
        error("string->symbol requires 1 argument", (*list)->line_num);
    
    if (args->car->type != TSTRING)
        error("string->symbol argument must be a string", (*list)->line_num);
        
    return intern(root, args->car->name);
}

static Obj *prim_exit(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("exit accepts 1 argument", (*list)->line_num);
    Obj *values = eval_list(root, env, list);
    Obj *first = values->car; 
    if (first->type != TINT)
        error("* must be an integer", (*list)->line_num);
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

static size_t read_file(char *fname, char **text) {
    size_t length = 0;
    FILE *f = fopen(fname, "r");
    if (!f) {
        printf("Failed to load file %s\n", fname);
        return 0;
    }

    filepos.filename = fname;
    fseek(f, 0, SEEK_END);
    length = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    *text = malloc(length + 1);
    if (!*text) {
        puts("Out of memory.");
        fclose(f);
        return 0;
    }

    size_t read = fread(*text, 1, length, f);
    if (read != length) {
        printf("Failed to read entire file %s\n", fname);
        free(*text);
        *text = NULL;
        fclose(f);
        return 0;
    }

    (*text)[length] = '\0';
    fclose(f);
    return length;
}

void process_file(char *fname, Obj **env, Obj **expr) {
    char *text = NULL;
    size_t len = read_file(fname, &text);
    if (len == 0) return;

    // Save old stdin
    FILE *old_stdin = stdin;
    // Create a single stream for the entire file
    FILE *stream = fmemopen(text, len, "r");
    if (!stream) {
        free(text);
        error("Failed to create memory stream for %s", (*expr)->line_num, fname);
        return;
    }

    // Redirect stdin to the memory stream
    stdin = stream;
    filepos.filename = fname;
    filepos.file_len = len;
    filepos.line_num = 1;

    int eval_input(void *root, Obj **env, Obj **expr);
    // Process expressions until we reach end of file
    while (!feof(stream)) {
        eval_input(gc_root, env, expr);
    }

    // Cleanup
    stdin = old_stdin;
    fclose(stream);
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

extern void *memory;

void init_minilisp(Obj **env) {
    // Memory allocation
    extern void *alloc_semispace();
    memory = alloc_semispace();

    // Constants and primitives
    Symbols = Nil;
    *env = make_env(NULL, &Nil, &Nil);
    define_constants(NULL, env);
    define_primitives(NULL, env);
}

int eval_input(void *root, Obj **env, Obj **expr) {
    if (setjmp(context) == 0) {
        while (true) {
            *expr = read_expr(root);         
            if (!*expr) 
                return 0;
            if (*expr == Cparen)
                error("Stray close parenthesis", (*expr)->line_num);
            if (*expr == Dot)
                error("Stray dot", (*expr)->line_num);
            print(eval(root, env, expr));
            putc('\n', stdout);
        }
    }
    return 0;
}
