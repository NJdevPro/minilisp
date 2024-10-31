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

__attribute((noreturn)) void error(char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    // Jump right back to the end of eval 
    longjmp(context, 1);
}

// Constants
static Obj *True = &(Obj){ TTRUE };
static Obj *Nil = &(Obj){ TNIL };
static Obj *Dot = &(Obj){ TDOT };
static Obj *Cparen = &(Obj){ TCPAREN };

//======================================================================
// Constructors
//======================================================================


// The list containing all symbols. Such data structure is traditionally called the "obarray", but I
// avoid using it as a variable name as this is not an array but a list.
Obj *Symbols;

void *root;    // root of memory

extern Obj *alloc(void *root, int type, size_t size);

static Obj *make_int(void *root, long long value) {
    Obj *r = alloc(root, TINT, sizeof(long long));
    r->value = value;
    return r;
}

static Obj *cons(void *root, Obj **car, Obj **cdr) {
    Obj *cell = alloc(root, TCELL, sizeof(Obj *) * 2);
    cell->car = *car;
    cell->cdr = *cdr;
    return cell;
}

static Obj *make_symbol(void *root, char *name) {
    Obj *sym = alloc(root, TSYMBOL, strlen(name) + 1);
    strcpy(sym->name, name);
    return sym;
}

static Obj *make_primitive(void *root, Primitive *fn) {
    Obj *r = alloc(root, TPRIMITIVE, sizeof(Primitive *));
    r->fn = fn;
    return r;
}

static Obj *make_function(void *root, Obj **env, int type, Obj **params, Obj **body) {
    assert(type == TFUNCTION || type == TMACRO);
    Obj *r = alloc(root, type, sizeof(Obj *) * 3);
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

// Returns ((x . y) . a)
static Obj *acons(void *root, Obj **x, Obj **y, Obj **a) {
    DEFINE1(cell);
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

static Obj *read_expr(void *root);

static int peek(void) {
    char c = getchar();
    ungetc(c, stdin);
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
        if (c == EOF || c == '\n')
            return;
        if (c == '\r') {
            if (peek() == '\n')
                getchar();
            return;
        }
    }
}

// Reads a list. Note that '(' has already been read.
static Obj *read_list(void *root) {
    DEFINE3(obj, head, last);
    *head = Nil;
    for (;;) {
        *obj = read_expr(root);
        if (!*obj)
            error("Unclosed parenthesis");
        if (*obj == Cparen)
            return reverse(*head);
        if (*obj == Dot) {
            *last = read_expr(root);
            if (read_expr(root) != Cparen)
                error("Closed parenthesis expected after dot");
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
    DEFINE1(sym);
    *sym = make_symbol(root, name);
    Symbols = cons(root, sym, &Symbols);
    return *sym;
}

// Reader marcro ' (single quote). It reads an expression and returns (quote <expr>).
static Obj *read_quote(void *root) {
    DEFINE2(sym, tmp);
    *sym = intern(root, "quote");
    *tmp = read_expr(root);
    *tmp = cons(root, tmp, &Nil);
    *tmp = cons(root, sym, tmp);
    return *tmp;
}

static long long read_number(int val) {
    while (isdigit(peek()))
        val = val * 10 + (getchar() - '0');
    return val;
}

static Obj *read_symbol(void *root, char c) {
    char buf[SYMBOL_MAX_LEN + 1];
    buf[0] = c;
    int len = 1;
    while (isalnum(peek()) || strchr(symbol_chars, peek())) {
        if (SYMBOL_MAX_LEN <= len)
            error("Symbol name too long");
        buf[len++] = getchar();
    }
    buf[len] = '\0';
    return intern(root, buf);
}

static Obj *read_expr(void *root) {
    for (;;) {
        char c = getchar();
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
            continue;
        if (c == EOF)
            return NULL;
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
        if (isdigit(c))
            return make_int(root, read_number(c - '0'));
        if (c == '-' && isdigit(peek()))
            return make_int(root, -read_number(0));
        if (isalpha(c) || strchr(symbol_chars, c))
            return read_symbol(root, c);
        error("Don't know how to handle %c", c);
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
    default:
        error("Bug: print: Unknown tag type: %d", obj->type);
    }
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
    DEFINE2(vars, tmp);
    *vars = (*env)->vars;
    *tmp = acons(root, sym, val, vars);
    (*env)->vars = *tmp;
}

// Returns a newly created environment frame.
static Obj *push_env(void *root, Obj **env, Obj **vars, Obj **vals) {
    DEFINE3(map, sym, val);
    *map = Nil;
    for (; (*vars)->type == TCELL; *vars = (*vars)->cdr, *vals = (*vals)->cdr) {
        if ((*vals)->type != TCELL)
            error("Cannot apply function: number of argument does not match");
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
    DEFINE2(lp, r);
    for (*lp = *list; *lp != Nil; *lp = (*lp)->cdr) {
        *r = (*lp)->car;
        *r = eval(root, env, r);
    }
    return *r;
}

// Evaluates all the list elements and returns their return values as a new list.
static Obj *eval_list(void *root, Obj **env, Obj **list) {
    DEFINE4(head, lp, expr, result);
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
    DEFINE3(params, newenv, body);
    *params = (*fn)->params;
    *newenv = (*fn)->env;
    *newenv = push_env(root, newenv, params, args);
    *body = (*fn)->body;
    return progn(root, newenv, body);
}

// Apply fn with args.
static Obj *apply(void *root, Obj **env, Obj **fn, Obj **args) {
    if (!is_list(*args))
        error("argument must be a list");
    if ((*fn)->type == TPRIMITIVE)
        return (*fn)->fn(root, env, args);
    if ((*fn)->type == TFUNCTION) {
        DEFINE1(eargs);
        *eargs = eval_list(root, env, args);
        return apply_func(root, env, fn, eargs);
    }
    error("not supported");
}

// Searches for a variable by symbol. Returns null if not found.
static Obj *find(Obj **env, Obj *sym) {
    for (Obj *p = *env; p != Nil; p = p->up) {
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
    DEFINE3(bind, macro, args);
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
    case TNIL:
        // Self-evaluating objects
        return *obj;
    case TSYMBOL: {
        // Variable
        Obj *bind = find(env, *obj);
        if (!bind)
            error("Undefined symbol: %s", (*obj)->name);
        return bind->cdr;
    }
    case TCELL: {
        // Function application form
        DEFINE3(fn, expanded, args);
        *expanded = macroexpand(root, env, obj);
        if (*expanded != *obj)
            return eval(root, env, expanded);
        *fn = (*obj)->car;
        *fn = eval(root, env, fn);
        *args = (*obj)->cdr;
        if ((*fn)->type != TPRIMITIVE && (*fn)->type != TFUNCTION)
            error("The head of a list must be a function");
        return apply(root, env, fn, args);
    }
    default:
        error("Bug: eval: Unknown tag type: %d", (*obj)->type);
    }
}

//======================================================================
// Primitive functions and special forms
//======================================================================

// 'expr
static Obj *prim_quote(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("Malformed quote");
    return (*list)->car;
}

// (cons expr expr)
static Obj *prim_cons(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2)
        error("Malformed cons");
    Obj *cell = eval_list(root, env, list);
    cell->cdr = cell->cdr->car;
    return cell;
}

// (car <cell>)
static Obj *prim_car(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (args->car->type != TCELL || args->cdr != Nil)
        error("Malformed car");
    return args->car->car;
}

// (cdr <cell>)
static Obj *prim_cdr(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (args->car->type != TCELL || args->cdr != Nil)
        error("Malformed cdr");
    return args->car->cdr;
}

// (setq <symbol> expr)
static Obj *prim_setq(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2 || (*list)->car->type != TSYMBOL)
        error("Malformed setq");
    DEFINE2(bind, value);
    *bind = find(env, (*list)->car);
    if (!*bind)
        error("Unbound variable %s", (*list)->car->name);
    *value = (*list)->cdr->car;
    *value = eval(root, env, value);
    (*bind)->cdr = *value;
    return *value;
}

// (setcar <cell> expr)
static Obj *prim_setcar(void *root, Obj **env, Obj **list) {
    DEFINE1(args);
    *args = eval_list(root, env, list);
    if (length(*args) != 2 || (*args)->car->type != TCELL)
        error("Malformed setcar");
    (*args)->car->car = (*args)->cdr->car;
    return (*args)->car;
}

// (while cond expr ...)
static Obj *prim_while(void *root, Obj **env, Obj **list) {
    if (length(*list) < 2)
        error("Malformed while");
    DEFINE2(cond, exprs);
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

// (length <cell> | ...)
static Obj *prim_length(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    int len = length(args);
    if (len != 1) {
        // number of arguments to length
    }
    else {
        Obj *lst = args->car;
        if (lst != Nil && lst->type != TCELL)
            error("When length has a single argument, it must be a list");
        for (len = 0; lst != Nil && lst->type == TCELL; lst = lst->cdr) 
            len++;
    }
    return make_int(root, len);
}

// (reverse ... | reverse <cell>)
static Obj *prim_reverse(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    int len = length(args);
    if (len > 1) {
        // reverse the arguments to reverse
        return reverse(args);
    }
    else { // reverse a list
        Obj *lst = args->car;
        if (lst != Nil && lst->type != TCELL)
            error("Argument to reverse must be a list");
        return reverse(lst);
    }
}

// (+ <integer> ...)
static Obj *prim_plus(void *root, Obj **env, Obj **list) {
    long long sum = 0;
    for (Obj *args = eval_list(root, env, list); args != Nil; args = args->cdr) {
        if (args->car->type != TINT)
            error("+ takes only numbers");
        sum += args->car->value;
    }
    return make_int(root, sum);
}

// (* <integer> ...)
static Obj *prim_mult(void *root, Obj **env, Obj **list) {
    long long prod = 1;
    for (Obj *args = eval_list(root, env, list); args != Nil; args = args->cdr) {
        if (args->car->type != TINT)
            error("* takes only numbers");
        prod *= args->car->value;
    }
    return make_int(root, prod);
}

// (/ <integer> ...)
static Obj *prim_div(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    long long r = args->car->value;
    for (Obj *p = args->cdr; p != Nil; p = p->cdr){
        if (p->car->type != TINT)
            error("/ takes only numbers");
        r /= p->car->value;
    }
    return make_int(root, r);
}

// (% <integer> ...)
static Obj *prim_modulo(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    long long r = args->car->value;
    for (Obj *p = args->cdr; p != Nil; p = p->cdr){
        if (p->car->type != TINT)
            error("mod takes only numbers");
        r %= p->car->value;
    }
    return make_int(root, r);
}

// (- <integer> ...)
static Obj *prim_minus(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    for (Obj *p = args; p != Nil; p = p->cdr)
        if (p->car->type != TINT)
            error("- takes only numbers");
    if (args->cdr == Nil)
        return make_int(root, -args->car->value);
    long long r = args->car->value;
    for (Obj *p = args->cdr; p != Nil; p = p->cdr)
        r -= p->car->value;
    return make_int(root, r);
}

// (< <integer> <integer>)
static Obj *prim_lt(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (length(args) != 2)
        error("malformed <");
    Obj *x = args->car;
    Obj *y = args->cdr->car;
    if (x->type != TINT || y->type != TINT)
        error("< takes only 2 numbers");
    return x->value < y->value ? True : Nil;
}

// (> <integer> <integer>)
static Obj *prim_gt(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (length(args) != 2)
        error("malformed >");
    Obj *x = args->car;
    Obj *y = args->cdr->car;
    if (x->type != TINT || y->type != TINT)
        error("> takes only 2 numbers");
    return x->value > y->value ? True : Nil;
}

// (<= <integer> <integer>)
static Obj *prim_lte(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (length(args) != 2)
        error("malformed <=");
    Obj *x = args->car;
    Obj *y = args->cdr->car;
    if (x->type != TINT || y->type != TINT)
        error("<= takes only 2 numbers");
    return x->value <= y->value ? True : Nil;
}

// (>= <integer> <integer>)
static Obj *prim_gte(void *root, Obj **env, Obj **list) {
    Obj *args = eval_list(root, env, list);
    if (length(args) != 2)
        error("malformed >=");
    Obj *x = args->car;
    Obj *y = args->cdr->car;
    if (x->type != TINT || y->type != TINT)
        error(">= takes only 2 numbers");
    return x->value >= y->value ? True : Nil;
}

// (not <cell>)
static Obj *prim_not(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("not accepts 1 argument");
    Obj *values = eval_list(root, env, list);
    return values->car == Nil ? True : Nil;
}

// (and ...)
static Obj *prim_and(void *root, Obj **env, Obj **list) {
    Obj *car = True;  // by default, return True if no args
    for (Obj *args = eval_list(root, env, list); args != Nil; args = args->cdr) {
        car = eval(root, env, &args->car);
        if (car == Nil) break;
    }
    return car;
}

// (or ...)
static Obj *prim_or(void *root, Obj **env, Obj **list) {
    Obj *car = Nil;
    for (Obj *args = eval_list(root, env, list); args != Nil; args = args->cdr) {
        car = eval(root, env, &args->car);
        if (car != Nil) break;
    }
    return car;
}

extern void process_file(char *fname, Obj **env, Obj **expr);

static Obj *prim_load(void *root, Obj **env, Obj **list) {
    DEFINE1(expr);
    Obj *args = eval_list(root, env, list);
    if (args->car->type != TSYMBOL){
        error("load: filename must be a symbol");
    }
    char *name = args->car->name;
    process_file(name, env, expr );
}

static Obj *prim_exit(void *root, Obj **env, Obj **list) {
    if (length(*list) != 1)
        error("exit accepts 1 argument");
    Obj *values = eval_list(root, env, list);
    Obj *first = values->car; 
    if (first->type != TINT)
        error("* must be an integer");
    exit(first->value);
}

static Obj *handle_function(void *root, Obj **env, Obj **list, int type) {
    if ((*list)->type != TCELL || !is_list((*list)->car) || (*list)->cdr->type != TCELL)
        error("Malformed lambda");
    Obj *p = (*list)->car;
    for (; p->type == TCELL; p = p->cdr)
        if (p->car->type != TSYMBOL)
            error("Parameter must be a symbol");
    if (p != Nil && p->type != TSYMBOL)
        error("Parameter must be a symbol");
    DEFINE2(params, body);
    *params = (*list)->car;
    *body = (*list)->cdr;
    return make_function(root, env, type, params, body);
}

// (lambda (<symbol> ...) expr ...)
static Obj *prim_lambda(void *root, Obj **env, Obj **list) {
    return handle_function(root, env, list, TFUNCTION);
}

static Obj *handle_defun(void *root, Obj **env, Obj **list, int type) {
    if ((*list)->car->type != TSYMBOL || (*list)->cdr->type != TCELL)
        error("Malformed defun");
    DEFINE3(fn, sym, rest);
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
        error("Malformed define");
    DEFINE2(sym, value);
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
        error("Malformed macroexpand");
    DEFINE1(body);
    *body = (*list)->car;
    return macroexpand(root, env, body);
}

// (print expr)
static Obj *prim_print(void *root, Obj **env, Obj **list) {
    DEFINE1(tmp);
    *tmp = (*list)->car;
    print(eval(root, env, tmp));
    return Nil;
}


// (println expr)
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
        error("Malformed if");
    DEFINE3(cond, then, els);
    *cond = (*list)->car;
    *cond = eval(root, env, cond);
    if (*cond != Nil) {
        *then = (*list)->cdr->car;
        return eval(root, env, then);
    }
    *els = (*list)->cdr->cdr;
    return *els == Nil ? Nil : progn(root, env, els);
}

// (= <integer> <integer>)
static Obj *prim_num_eq(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2)
        error("Malformed =");
    Obj *values = eval_list(root, env, list);
    Obj *x = values->car;
    Obj *y = values->cdr->car;
    if (x->type != TINT || y->type != TINT)
        error("= only takes numbers");
    return x->value == y->value ? True : Nil;
}

// (eq expr expr)
static Obj *prim_eq(void *root, Obj **env, Obj **list) {
    if (length(*list) != 2)
        error("Malformed eq");
    Obj *values = eval_list(root, env, list);
    return values->car == values->cdr->car ? True : Nil;
}

static void add_primitive(void *root, Obj **env, char *name, Primitive *fn) {
    DEFINE2(sym, prim);
    *sym = intern(root, name);
    *prim = make_primitive(root, fn);
    add_variable(root, env, sym, prim);
}

static void define_constants(void *root, Obj **env) {
    DEFINE1(sym);
    *sym = intern(root, "t");
    add_variable(root, env, sym, &True);
}

static void define_primitives(void *root, Obj **env) {
    add_primitive(root, env, "quote", prim_quote);
    add_primitive(root, env, "cons", prim_cons);
    add_primitive(root, env, "car", prim_car);
    add_primitive(root, env, "cdr", prim_cdr);
    add_primitive(root, env, "setq", prim_setq);
    add_primitive(root, env, "setcar", prim_setcar);
    add_primitive(root, env, "while", prim_while);
    add_primitive(root, env, "gensym", prim_gensym);
    add_primitive(root, env, "length", prim_length);
    add_primitive(root, env, "reverse", prim_reverse);
    add_primitive(root, env, "+", prim_plus);
    add_primitive(root, env, "-", prim_minus);
    add_primitive(root, env, "*", prim_mult);
    add_primitive(root, env, "/", prim_div);
    add_primitive(root, env, "mod", prim_modulo);
    add_primitive(root, env, "=", prim_num_eq);
    add_primitive(root, env, "eq", prim_eq);
    add_primitive(root, env, "<", prim_lt);
    add_primitive(root, env, ">", prim_gt);
    add_primitive(root, env, "<=", prim_lte);
    add_primitive(root, env, ">=", prim_gte);
    add_primitive(root, env, "not", prim_not);
    add_primitive(root, env, "and", prim_and);
    add_primitive(root, env, "or", prim_or);
    add_primitive(root, env, "define", prim_define);
    add_primitive(root, env, "defun", prim_defun);
    add_primitive(root, env, "defmacro", prim_defmacro);
    add_primitive(root, env, "macroexpand", prim_macroexpand);
    add_primitive(root, env, "lambda", prim_lambda);
    add_primitive(root, env, "if", prim_if);
    add_primitive(root, env, "progn", prim_progn);
    add_primitive(root, env, "print", prim_print);
    add_primitive(root, env, "println", prim_println);
    add_primitive(root, env, "load", prim_load);
    add_primitive(root, env, "exit", prim_exit);
}

//======================================================================
// Entry point
//======================================================================

void init_minilisp(Obj **env) {
    // Memory allocation
    extern void *memory;
    extern void *alloc_semispace();
    memory = alloc_semispace();

    // Constants and primitives
    Symbols = Nil;
    root = NULL;
    *env = make_env(root, &Nil, &Nil);
    define_constants(root, env);
    define_primitives(root, env);
}

int eval_input(char *input, Obj **env, Obj **expr) {
    if (setjmp(context) == 0){
        *expr = read_expr(input);
        if (!*expr)
            return 0;
        if (*expr == Cparen)
            error("Stray close parenthesis");
        if (*expr == Dot)
            error("Stray dot");
        print(eval(root, env, expr));
        puts("");
        return 0;
    }
    else return 1;
}
