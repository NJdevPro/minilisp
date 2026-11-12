MiniLisp with REPL
==================

This is my attempt at making Rui Ueyama (rui314)'s MiniLisp more user friendly and powerful, while trying to keep the goal of simplicity and conciseness.
Not being limited by the 1000 lines challenge, I've added a Read-Eval-Print-Loop (REPL) and a number of primitives to the original program to make it a comfortable Lisp implementation.

The whole program is 2500 lines of C and compiles to around 120 kb without debugging symbols. It should be able to run on low powered devices.

The original MiniLisp included the following:
- integers, symbols, cons cells
- global variables,
- lexically-scoped local variables,
- closures,
- _if_ conditional,
- primitive functions, such as +, =, <, or _list_,
- user-defined functions,
- a macro system,
- and a copying garbage collector.

The added primitives of this version:
- cons, car, cdr
- strings and conversion
- floats and operators on floats
- vectors
- operators *, /, mod
- predicates >, >=, <=, not,
- special forms and, or,
- functions list, atom, length, reverse, progn, print, println, load, exit, gensym
- tail call optimization

This added expressivity has the side effect of being much faster as well, since all these primitives are compiled C instead of being interpreted.

In this version, instead of passing a file using pipes, you simply pass the files as command parameters :
./minilisp f1 f2 etc

The files all share the same environment, so all the symbols, functions and macros defined 
in f1 can be reused in the following files. The REPL is started after execution, unless we pass the option
-r to the command line, in which case the interpreter quits immediately at the end.

You can also pass a simple Lisp command as parameter:
./minilisp -x "(+ 1 1)"

The Lisp command will be evaluated and then the REPL is started. 
However we can quit immediately after execution with -r (or --no-repl).

The options, which can be given before or after the file names, are:

```
-x, --exec CODE   evaluate the Lisp code CODE
-r, --no-repl     don't enter the read-eval-print loop
-H, --no-history  don't load nor save the history of the REPL
-h, --help        print the help and exit
```

When an expression fails, the error is reported on the standard error output
as `file[line]: message` and the evaluation goes on with the next expression.

The Read-Eval-Print-Loop (REPL) is based on Justine Tunney (jart)'s bestline.

## REPL Shortcuts

```
CTRL-Enter     CONTINUE ON NEXT LINE
CTRL-E         END
CTRL-A         START
CTRL-B         BACK
CTRL-F         FORWARD
CTRL-L         CLEAR
CTRL-H         BACKSPACE
CTRL-D         DELETE
CTRL-Y         YANK
CTRL-D         EOF (IF EMPTY)
CTRL-N         NEXT HISTORY
CTRL-P         PREVIOUS HISTORY
CTRL-R         SEARCH HISTORY
CTRL-G         CANCEL SEARCH
ALT-<          BEGINNING OF HISTORY
ALT->          END OF HISTORY
ALT-F          FORWARD WORD
ALT-B          BACKWARD WORD
CTRL-ALT-F     FORWARD EXPR
CTRL-ALT-B     BACKWARD EXPR
ALT-RIGHT      FORWARD EXPR
ALT-LEFT       BACKWARD EXPR
CTRL-K         KILL LINE FORWARDS
CTRL-U         KILL LINE BACKWARDS
ALT-H          KILL WORD BACKWARDS
CTRL-W         KILL WORD BACKWARDS
CTRL-ALT-H     KILL WORD BACKWARDS
ALT-D          KILL WORD FORWARDS
ALT-Y          ROTATE KILL RING AND YANK AGAIN
ALT-\          SQUEEZE ADJACENT WHITESPACE
CTRL-T         TRANSPOSE
ALT-T          TRANSPOSE WORD
ALT-U          UPPERCASE WORD
ALT-L          LOWERCASE WORD
ALT-C          CAPITALIZE WORD
CTRL-C         INTERRUPT PROCESS
CTRL-Z         SUSPEND PROCESS
CTRL-\         QUIT PROCESS
CTRL-S         PAUSE OUTPUT
CTRL-Q         UNPAUSE OUTPUT (IF PAUSED)
CTRL-Q         ESCAPED INSERT
CTRL-SPACE     SET MARK
CTRL-X CTRL-X  GOTO MARK
CTRL-Z         SUSPEND PROCESS
```

CTRL-D on an empty line leaves the REPL. Lines starting with a slash are REPL commands:

```
/memory        display the amount of memory used and the size of the heap
/help          display a short help
```

The REPL also saves the history of commands in the file history.txt
This file is loaded at startup, so one can recall previous commands
(unless the option -H is given).

Known bugs and limitations:
* recall of multiline commands does not work as expected.
* a recursion which is not a tail call (such as `(+ 1 (f (- n 1)))`) uses the C stack:
  deeper than about 25000 calls it stops with the error "Stack overflow: the
  recursion is too deep".

Compile
-------

    $ make

MiniLisp has been tested on Linux x86/x86-64 and 64 bit Mac OS. The code is not
very architecture dependent, so you should be able to compile and run on other
Unix-like operating systems.

Test
----

MiniLisp comes with a comprehensive test suite. In order to run the tests, give
"test" argument to make.

    $ make test

The tests run twice, the second time with the environment variable
`MINILISP_ALWAYS_GC=1`, which makes the garbage collector run at every memory
allocation and makes the old heap inaccessible, so that any stale pointer
crashes immediately. `MINILISP_DEBUG_GC=1` prints a line on the standard error
output at each collection. The heap starts at 4 MB (per semi-space) and doubles
when more than half of it is still in use after a collection.

Language features
-----------------

MiniLisp is a traditional Lisp interpreter. It reads one expression at a time
from a file, from the `-x` option or from the REPL, evaluates it, and then
prints out the return value of the expression. Here is an example of a valid
input.

    (+ 1 2)

The above expression prints "3".

### Literals

MiniLisp supports integer literals, float literals, string literals, vector literals, `()`, `t`, symbols, and
list literals.

* Integer literals are positive or negative 64 bit integers.
* Float literals are double precision numbers. They have a fractional part, an
  exponent, or both: `1.5`, `-0.25`, `1e3`, `2.5E-4`. A digit is required on
  both sides of the point, so `1.` and `.5` are not floats.
* Vector literals are written between square brackets: `[1 2 3]`. Contrary to the
  list literals, their elements are evaluated (see Vectors below).
* String literals are written between double quotes. The escape sequences `\n`,
  `\t`, `\r`, `\"` and `\\` are recognized.
* `()` is the only false value. It also represents the empty list.
* `t` is a predefined variable evaluated to itself. It's a preferred way to
  represent a true value, while any non-`()` value is considered to be true.
* Symbols are objects with unique name. They are used to represent identifiers.
* List literals are cons cells. It's either a regular list whose last element's
  cdr is `()` or an dotted list ending with any non-`()` value. A dotted list is
  written as `(a . b)`.

### List operators

`cons` takes two arguments and returns a new cons cell, making the first
argument the car, and the second the cdr.

    (cons 'a 'b)   ; -> (a . b)
    (cons 'a '(b)) ; -> (a b)

`car` and `cdr` are accessors for cons cells. `car` returns the car, and `cdr`
returns the cdr.

    (car '(a . b)) ; -> a
    (cdr '(a . b)) ; -> b

`setcar` mutates a cons cell. `setcar` takes two arguments, assuming the first
argument is a cons cell. It sets the second argument's value to the cons cell's
car.

    (define cell (cons 'a 'b))
    cell  ; -> (a . b)
    (setcar cell 'x)
    cell  ; -> (x . b)

`length` and `reverse` operate either on their arguments, or a single list or a string
(`length` also accepts a vector).
`reverse` never modifies its arguments: it returns a new list or a new string.

    (length '(1 2 3))   ; -> 3
    (length 1 2 t)      ; -> 3
    (length "1 2 3")    ; -> 5

    (reverse '(a b c))  ; -> (c b a)
    (reverse 1 2 3)     ; -> (3 2 1)
    (reverse "1234")    ; -> "4321" 
    (reverse '((a) b "c"))  ; -> (c b (a))

### Numeric operators

Integers are 64 bit signed integers. The result of an operation that overflows
wraps around. Floats are double precision numbers. Numeric operators accept
integers and floats: when at least one argument is a float, the integers are
converted and the result is a float, otherwise the result is an integer.

`+` returns the sum of the arguments.

    (+)        ; -> 0
    (+ 1)      ; -> 1
    (+ 1 2)    ; -> 3
    (+ 1 2 3)  ; -> 6

`*` returns the product of the arguments.

    (*)        ; -> 1
    (* 2 3 4)  ; -> 24

`-` negates the value of the argument if only one argument is given.

    (- 3)      ; -> -3
    (- -5)     ; -> 5

If multiple arguments are given, `-` subtracts each argument from the first one.

    (- 5 2)    ; -> 3
    (- 5 2 7)  ; -> -4

`/` divides the first argument by each of the following ones, and `mod` returns
the remainder of the same division. The division of integers is truncated
towards zero (the remainder has the sign of the dividend), the one of floats is
not. Dividing by zero is an error.

    (/ 7 2)    ; -> 3
    (/ -7 2)   ; -> -3
    (mod 7 2)  ; -> 1
    (mod -7 2) ; -> -1
    (/ 7 2.0)  ; -> 3.5
    (mod 7.5 2) ; -> 1.5

`=` takes two arguments and returns `t` if the two numbers are equal, whatever
their types.

    (= 11 11)  ; -> t
    (= 11 6)   ; -> ()
    (= 1 1.0)  ; -> t

`<` takes two arguments and returns `t` if the first argument is smaller than
the second.

    (< 2 3)    ; -> t
    (< 3 3)    ; -> ()
    (< 4 3)    ; -> ()

The other numerical predicates `>`, `<=`, `>=` work in a similar fashion.

### Conditionals

`(if cond then else ...)` is the only conditional in the language. It first
evaluates *cond*. If the result is a true value, *then* is evaluated. Otherwise
the *else* expressions are evaluated in sequence, and the value of the last one
is returned (`()` if there is none).

`(and expr ...)` evaluates its expressions from left to right and stops at the first
one which is `()`, which is then the result. Otherwise the result is the value of the last
expression. `(and)` is `t`.

`(or expr ...)` evaluates its expressions from left to right and stops at the first
one which is not `()`, which is then the result. If there is none the result is `()`.

    (and 1 2 3)       ; -> 3
    (and 1 () (car 1)) ; -> ()   the third expression is not evaluated
    (or () 'a 'b)     ; -> a

The last expression of `and` and `or` is in tail position.

### Loops

`(while cond expr ...)` executes `expr ...` until `cond` is evaluated to
`()`. This is the only loop supported by MiniLisp.

If you are familiar with Scheme, you might be wondering if you could write a
loop by tail recursion in MiniLisp. The answer is yes: a call in tail position
does not consume any stack space. This is the case of the last expression of the
body of a function, of `progn`, of the branches of `if`, and of whatever a macro
expands to such an expression. So a loop can be written as a recursion with
an accumulator, and run for as long as needed:

    (defun sum (n acc)
      (if (= n 0)
          acc
          (sum (- n 1) (+ n acc))))   ; tail call
    (sum 1000000 0)                   ; -> 500000500000

A recursion which is not a tail call, such as `(+ 1 (f (- n 1)))`, still needs
a stack frame for each level and fails with the "Stack overflow" error after
about 25000 calls.

### Imperative programming

`(progn expr expr ...)` executes several expressions in sequence.

    (progn (print "I own ")
           (defun add (x y) (+ x y))
           (print (add 3 7))
           (println " cents"))  ; -> prints "I own 10 cents"

### Floats

A float is always printed with a point or an exponent (`2.0`, `1e+21`), and with
as many digits as needed to read it back unchanged. Overflows give `inf` or
`-inf`.

`float` converts a number to a float. `truncate` (towards zero), `floor` and
`round` (to the nearest integer, halves away from zero) convert a number to an
integer, and are an error if the float does not fit in 64 bits.

    (float 3)         ; -> 3.0
    (truncate -2.7)   ; -> -2
    (floor -2.7)      ; -> -3
    (round 2.5)       ; -> 3

`sqrt`, `exp`, `log`, `sin`, `cos` and `atan` take one number and return a
float. `(expt x y)` returns `x` raised to the power `y`. `sqrt` of a negative
number and `log` of a number which is not positive are errors.

    (sqrt 2)          ; -> 1.4142135623730951
    (expt 2 10)       ; -> 1024.0

`float?` and `integer?` tell the type of a number.

    (float? 1.5)      ; -> t
    (integer? 1.5)    ; -> ()

### Vectors

A vector is a fixed size sequence of values, whose elements are reached in
constant time, unlike those of a list. The first element has the index 0.

`[a b c]` is read as `(vector a b c)`. The elements are evaluated, and each
evaluation makes a new vector, so a vector written in a function body is never
shared between two calls. To have symbols or lists in a vector, quote them.

    [1 (+ 1 1) "three"]    ; -> [1 2 three]
    ['a '(b c)]            ; -> [a (b c)]
    (vector 1 2)           ; -> [1 2]

`(make-vector n)` makes a vector of `n` elements which are `()`, and
`(make-vector n x)` one whose elements are all `x`.

    (make-vector 3 0)      ; -> [0 0 0]

`aref` reads an element, `aset` stores a value in the vector and returns it.
An index out of range is an error.

    (define v (make-vector 3 0))
    (aset v 1 'x)          ; -> x
    (aref v 1)             ; -> x
    v                      ; -> [0 x 0]

`length` returns the number of elements, `vector?` tells whether an object is a
vector, and `vector->list` and `list->vector` convert between lists and vectors.

    (length [1 2 3])       ; -> 3
    (vector? [1])          ; -> t
    (vector->list [1 2 3]) ; -> (1 2 3)
    (list->vector '(a b))  ; -> [a b]

Note that `'[a b]` is the list `(vector a b)`, not a vector of symbols: write
`['a 'b]` or `(list->vector '(a b))`. Two vectors are `eq` only if they are the
same object.

### Equivalence test operators

`eq` takes two arguments and returns `t` if the objects are the same. Integers
and floats are compared by value (but an integer is never `eq` to a float, as
opposed to `=`), and strings by content. For all the other objects `eq` does
a pointer comparison, so two lists that happened to have the same contents but
are actually different are considered to not be the same by `eq`.

    (eq 1000 1000)          ; -> t
    (eq 'a 'a)              ; -> t
    (eq '(1) '(1))          ; -> ()

### String functions

`eq` compares strings by content.

    (eq "Hello" "Hello")    ; -> t
    (eq "Hello" "hello")    ; -> ()
    
`string-concat` concatenates strings.

    (string-concat)                 ; -> ""
    (string-concat "A" "B" "C" "D") ; -> "ABCD"

`symbol->string` turns a symbol into a string.
    
    (define sym 'hello)    ; -> hello
    (symbol->string sym)   ; -> "hello"

`string->symbol` turns a string into a symbol of the same name.

    (string->symbol "hello")   ; -> hello

### Output operators

`print` prints its arguments to the standard output, separated by a space.
Strings are printed without the quotes.

    (print 3)               ; -> "3"
    (print '(hello world))  ; -> "(hello world)"
    (print "hello" "world") ; -> "hello world"

`println` does the same, adding a return at the end. Without argument, it only
prints a return.

### Definitions

MiniLisp supports variables and functions. They can be defined using `define`.

    (define a (+ 1 2))
    (+ a a)   ; -> 6

There are two ways to define a function. One way is to use a special form
`lambda`. `(lambda (args ...)  expr ...)` returns a function object which
you can assign to a variable using `define`.

    (define double (lambda (x) (+ x x)))
    (double 6)                ; -> 12
    ((lambda (x) (+ x x)) 6)  ; do the same thing without assignment

The other way is `defun`. `(defun fn (args ...) expr ...)` is short for
`(define fn (lambda (args ...) expr ...))`.

    ;; Define "double" using defun
    (defun double (x) (+ x x))

You can write a function that takes variable number of arguments. If the
parameter list is a dotted list, the remaining arguments are bound to the last
parameter as a list.

    (defun fn (expr . rest) rest)
    (fn 1)     ; -> ()
    (fn 1 2 3) ; -> (2 3)

Variables are lexically scoped and have indefinite extent. References to "outer"
variables remain valid even after the function that created the variables
returns. Calling a function with more or fewer arguments than it has
parameters is an error, unless the last parameter is a dotted one as above.

    ;; A countup function. We use lambda to introduce local variables because we
    ;; do not have "let" and the like. (The file examples/library.lisp defines
    ;; "let", "when", "and", "or" etc. as macros).
    (define counter
      ((lambda (count)
         (lambda ()
           (setq count (+ count 1))
           count))
       0))

    (counter)  ; -> 1
    (counter)  ; -> 2

    ;; This will not return 12345 but 3. Variable "count" in counter function
    ;; is resolved based on its lexical context rather than dynamic context.
    ((lambda (count) (counter)) 12345)  ; -> 3

`setq` sets a new value to an existing variable. It's an error if the variable
is not defined.

    (define val (+ 3 5))
    (setq val (+ val 1))  ; increment "val"

### Introspection

`atom` returns () if the argument is a cell, t otherwise.

    (atom '(a b))   ; -> ()
    (atom "")       ; -> t
    (atom ())       ; -> t
    
### System functions
`load` loads a Lisp file and evaluates all its content, adding it to the environment.

    (load "examples/nqueens.lisp") -> run the file and store its evaluated functions
                                      and macros

A relative path is first looked up in the directory of the file that calls `load`,
then in the current directory. For instance, `(load "./library.lisp")` in
examples/hanoi.lisp loads examples/library.lisp, wherever minilisp is started from.

`exit` quits the interpreter and returns the integer passed as parameter.

    (exit 0) -> quit with success

### Macros

Macros look similar to functions, but they are different that macros take an
expression as input and returns a new expression as output. `(defmacro
macro-name (args ...) body ...)` defines a macro. Here is an example.

    (defmacro unless (condition expr)
      (list 'if condition () expr))

The above `defmacro` defines a new macro *unless*. *unless* is a new conditional
which evaluates *expr* unless *condition* is a true value. You cannot do the
same thing with a function because all the arguments would be evaluated before
the control is passed to the function.

    (define x 0)
    (unless (= x 0) '(x is not 0))  ; -> ()
    (unless (= x 1) '(x is not 1))  ; -> (x is not 1)

`macroexpand` is a convenient special form to see the expanded form of a macro.

    (macroexpand (unless (= x 1) '(x is not 1)))
    ;; -> (if (= x 1) () (quote (x is not 1)))

`gensym` creates a new symbol which will never be `eq` to any other symbol other
than itself. Useful for writing a macro that introduces new identifiers.

    (gensym)   ; -> a new symbol, such as G__0

### Comments

As in the traditional Lisp syntax, `;` (semicolon) starts a single line comment.
The comment continues to the end of line.
