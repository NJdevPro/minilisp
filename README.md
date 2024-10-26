MiniLisp with REPL
==================

Foreword by N. Janin:
This is my attempt at making rui314' MiniLisp slightly more user friendly.
Not being limited by the 1000 lines challenge, I've added a few basic primitives 
to the original program, while trying to keep the goal of simplicity and conciseness.
Namely operators >, >=, <=, or, and, not, and function length.
This has the side effect of being faster as well, since these primitives are compiled 
instead of being interpreted.

Among the bells and whistles, I've added a REPL based on jart's bestline.

## Shortcuts

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

The REPL also saves the history of commands in the file history.txt

Known bugs:
* Operators "and" and "or" do not work like their typical Lisp counterpart 
because they evaluate all their operands at the same time instead of one
by one.
* The pasting in the REPL doesn't work well.
* Multiline inputs are recalled as different lines


Original README
---------------

One day I wanted to see what I can do with 1k lines of C and
decided to write a Lisp interpreter. That turned to be a
fun weekend project, and the outcome is a mini lisp implementation
that supports

- integers, symbols, cons cells,
- global variables,
- lexically-scoped local variables,
- closures,
- _if_ conditional,
- primitive functions, such as +, =, <, or _list_,
- user-defined functions,
- a macro system,
- and a copying garbage collector.

All those in 1000 lines of C. I didn't sacrifice readability for size.
The code is in my opinion heavily commented to help the reader understand
how all these features work.

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

Language features
-----------------

MiniLisp is a traditional Lisp interpreter. It reads one expression at a time
from the standard input, evaluates it, and then prints out the return value of
the expression. Here is an example of a valid input.

    (+ 1 2)

The above expression prints "3".

### Literals

MiniLisp supports integer literals, `()`, `t`, symbols, and list literals.

* Integer literals are positive or negative integers.
* `()` is the only false value. It also represents the empty list.
* `t` is a predefined variable evaluated to itself. It's a preferred way to
  represent a true value, while any non-`()` value is considered to be true.
* Symbols are objects with unique name. They are used to represent identifiers.
  Because MiniLisp does not have string type, symbols are sometimes used as a
  substitute for strings too.
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

### Numeric operators

`+` returns the sum of the arguments.

    (+ 1)      ; -> 1
    (+ 1 2)    ; -> 3
    (+ 1 2 3)  ; -> 6

`-` negates the value of the argument if only one argument is given.

    (- 3)      ; -> -3
    (- -5)     ; -> 5

If multiple arguments are given, `-` subtracts each argument from the first one.

    (- 5 2)    ; -> 3
    (- 5 2 7)  ; -> -4

`=` takes two arguments and returns `t` if the two are the same integer.

    (= 11 11)  ; -> t
    (= 11 6)   ; -> ()

`<` takes two arguments and returns `t` if the first argument is smaller than
the second.

    (< 2 3)    ; -> t
    (< 3 3)    ; -> ()
    (< 4 3)    ; -> ()

### Conditionals

`(if cond then else)` is the only conditional in the language. It first
evaluates *cond*. If the result is a true value, *then* is evaluated. Otherwise
*else* is evaluated.

### Loops

`(while cond expr ...)` executes `expr ...` until `cond` is evaluated to
`()`. This is the only loop supported by MiniLisp.

If you are familiar with Scheme, you might be wondering if you could write a
loop by tail recursion in MiniLisp. The answer is no. Tail calls consume stack
space in MiniLisp, so a loop written as recursion will fail with the memory
exhaustion error.

### Equivalence test operators

`eq` takes two arguments and returns `t` if the objects are the same. What `eq`
really does is a pointer comparison, so two objects happened to have the same
contents but actually different are considered to not be the same by `eq`.

### Output operators

`println` prints a given object to the standard output.

    (println 3)               ; prints "3"
    (println '(hello world))  ; prints "(hello world)"

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
`(define fn (lambda (args ...) expr ...)`.

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
returns.

    ;; A countup function. We use lambda to introduce local variables because we
    ;; do not have "let" and the like.
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

    (gensym)   ; -> a new symbol

### Comments

As in the traditional Lisp syntax, `;` (semicolon) starts a single line comment.
The comment continues to the end of line.

No GC Branch
------------

There is a MiniLisp branch from which the code for garbage collection has been
stripped. The accepted language is the same, but the code is simpler than the
master branch's one. The reader might want to read the nogc branch first, then
proceed to the master branch, to understand the code step by step.

The nogc branch is available at
[nogc](https://github.com/rui314/minilisp/tree/nogc). The original is available
at [master](https://github.com/rui314/minilisp).
