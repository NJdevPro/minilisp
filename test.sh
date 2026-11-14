#!/bin/bash

function fail() {
  echo -n -e '\e[1;31m[ERROR]\e[0m '
  echo "$1"
  exit 1
}

function do_run() {
  error=$(echo "$3" | ./minilisp -r -x "${3}" 2>&1 > /dev/null)
  if [ -n "$error" ]; then
    echo FAILED
    fail "$error"
  fi

  result=$(echo "$3" | ./minilisp -r -x "${3}" 2> /dev/null | tail -1)
  if [ "$result" != "$2" ]; then
    echo FAILED
    fail "$2 expected, but got $result"
  fi
}

function run() {
  echo -n "Testing $1 ... "
  # Run the tests twice to test the garbage collector with different settings.
  MINILISP_ALWAYS_GC= do_run "$@"
  MINILISP_ALWAYS_GC=1 do_run "$@"
  echo ok
}

# Basic data types
run integer 1 1
run integer -1 -1
run symbol a "'a"
run quote a "(quote a)"
run quote 63 "'63"
run quote '(+ 1 2)' "'(+ 1 2)"

run + 3 '(+ 1 2)'
run + -2 '(+ 1 -3)'
run '*' 24 '(* 1 2 3 4)'
run '*' -24 '(* -1 2 3 4)'
run '/' 6 '(/ 13 2)'
run 'mod' 1 '(mod 5 2)'
run 'mod' 0 '(mod 4 2)'

run 'unary -' -3 '(- 3)'
run '-' -2 '(- 3 5)'
run '-' -9 '(- 3 5 7)'

run '<' t '(< 2 3)'
run '<' '()' '(< 3 3)'
run '<' '()' '(< 4 3)'

run list '(1 2 3 a b)' "(list 1 2 3 'a 'b)"
run 'literal list' '(a b c)' "'(a b c)"
run 'literal list' '(a b . c)' "'(a b . c)"

run reverse '(3 2 1)' '(reverse 1 2 3)'
run reverse "(3 2 1)" "(reverse '(1 2 3))"
run reverse "cba" '(reverse "abc")'

run length 5 '(length 1 2 3 4 5)'
run length 5 "(length '(1 2 3 4 5))"
run length 5 '(length "abcde")'

# List manipulation
run cons "(a . b)" "(cons 'a 'b)"
run cons "(a b c)" "(cons 'a (cons 'b (cons 'c ())))"

run car a "(car '(a b c))"
run cdr "(b c)" "(cdr '(a b c))"

run setcar "(x . b)" "(define obj (cons 'a 'b)) (setcar obj 'x) obj"

# Comments
run comment 5 "
  ; 2
  5 ; 3"

# Global variables
run define 7 '(define x 7) x'
run define 10 '(define x 7) (+ x 3)'
run define 7 '(define + 7) +'
run setq 11 '(define x 7) (setq x 11) x'
run setq 17 '(setq + 17) +'

# Conditionals
run if a "(if 1 'a)"
run if '()' "(if () 'a)"
run if a "(if 1 'a 'b)"
run if a "(if 0 'a 'b)"
run if a "(if 'x 'a 'b)"
run if b "(if () 'a 'b)"
run if c "(if () 'a 'b 'c)"

# Numeric comparisons
run = t '(= 3 3)'
run = '()' '(= 3 2)'

run '<' t '(< 2 3)'
run '<' '()' '(< 3 3)'
run '<' '()' '(< 4 3)'

run '>' t '(> 3 2)'
run '>' t '(> 3 -1)'
run '>' '()' '(> 3 3)'
run '>' '()' '(> 2 3)'
run '>' 't' '(> 4 3)'

run '>=' 't' '(>= 3 2)'
run '>=' 't' '(>= 3 -1)'
run '>=' 't' '(>= 3 3)'
run '>=' '()' '(>= 2 3)'
run '>=' 't' '(>= 4 3)'

run '<=' '()' '(<= 3 2)'
run '<=' '()' '(<= 3 -1)'
run '<=' 't' '(<= 3 3)'
run '<=' 't' '(<= 2 3)'
run '<=' '()' '(<= 4 3)'

# eq
run eq t "(eq 'foo 'foo)"
run eq t "(eq + +)"
run eq '()' "(eq 'foo 'bar)"
run eq '()' "(eq + 'bar)"

# gensym
run gensym G__0 '(gensym)'
run gensym '()' "(eq (gensym) 'G__0)"
run gensym '()' '(eq (gensym) (gensym))'
run gensym t '((lambda (x) (eq x x)) (gensym))'

# Functions
run lambda '<function>' '(lambda (x) x)'
run lambda t '((lambda () t))'
run lambda 9 '((lambda (x) (+ x x x)) 3)'
run defun 12 '(defun double (x) (+ x x)) (double 6)'

run args 15 '(defun f (x y z) (+ x y z)) (f 3 5 7)'

run restargs '(3 5 7)' '(defun f (x . y) (cons x y)) (f 3 5 7)'
run restargs '(3)'    '(defun f (x . y) (cons x y)) (f 3)'

# check_heap NAME EXPECTED OPTIONS CODE: the last line of the output must be EXPECTED.
function check_heap() {
  echo -n "Testing $1 ... "
  for gc in "" 1; do
    result=$(MINILISP_ALWAYS_GC=$gc ./minilisp $3 -r -H -x "$4" </dev/null 2>&1 | sed 's/Command history disengaged.//' | grep -v '^$' | tail -1)
    if [ "$result" != "$2" ]; then
      echo FAILED
      fail "$2 expected, but got $result"
    fi
  done
  echo ok
}

# check_io NAME EXPECTED STDIN CODE [ARG ...]: runs the code with the text STDIN (given to
# printf) as standard input and the ARGs after --, and checks the last line of the output.
function check_io() {
  echo -n "Testing $1 ... "
  for gc in "" 1; do
    result=$(printf "$3" | MINILISP_ALWAYS_GC=$gc ./minilisp -r -H -x "$4" -- "${@:5}" 2>&1 | sed 's/Command history disengaged.//' | grep -v '^$' | tail -1)
    if [ "$result" != "$2" ]; then
      echo FAILED
      fail "$2 expected, but got $result"
    fi
  done
  echo ok
}

# check_err NAME EXPECTED CODE: the last line written on the standard error must be EXPECTED
# (Not run with MINILISP_ALWAYS_GC: building a very deep list would take forever.)
function check_err() {
  echo -n "Testing $1 ... "
  result=$(./minilisp -r -H -x "$3" </dev/null 2>&1 >/dev/null | tail -1)
  if [ "$result" != "$2" ]; then
    echo FAILED
    fail "$2 expected, but got $result"
  fi
  echo ok
}

# strings
run 'string-concat' 'one & two and 3' '(string-concat "one" " & " "two" " and " 3)'
run 'symbol->string' 'twelve' "
  (define twelve 12)
  (symbol->string 'twelve)"
run 'string->symbol' 'twelve' '(string->symbol "twelve")'
run 'eq string and nil' '()' '(eq "a" ())'
run 'eq nil and string' '()' '(eq () "a")'
run 'eq string and number' '()' '(eq "1" 1)'
run 'substring' 'el' '(substring "hello" 1 3)'
run 'substring to the end' 'llo' '(substring "hello" 2)'
run 'substring empty' '' '(substring "hello" 5)'
run 'substring whole' 'hello' '(substring "hello" 0 5)'
run 'string-ref' 'o' '(string-ref "hello" 4)'
run 'list->string' 'a1b' '(list->string (list "a" 1 "b"))'
run 'list->string empty' '' '(list->string ())'
run 'string->number int' 42 '(string->number "42")'
run 'string->number negative' -7 '(string->number "-7")'
run 'string->number plus' 7 '(string->number "+7")'
run 'string->number float' -350.0 '(string->number "-3.5e2")'
run 'string->number not a number' '()' '(string->number "4x")'
run 'string->number empty' '()' '(string->number "")'
run 'string->number dot' '()' '(string->number "1.")'
run 'string->number sign only' '()' '(string->number "-")'
run 'string->number too large' '()' '(string->number "99999999999999999999")'
run 'string <' t '(< "abc" "abd")'
run 'string <' '()' '(< "b" "a")'
run 'string >=' t '(>= "b" "b")'
run 'string = ' t '(= "x" "x")'
run 'string gc' 200 '
  (define s "")
  (define i 0)
  (while (< i 200) (setq s (string-concat s (substring "xyz" 1 2))) (setq i (+ i 1)))
  (length s)'
check_heap 'substring range' '[1]: substring: the range 2 to 9 is out of the string (the length is 5)' '' '(substring "hello" 2 9)'
check_heap 'substring reversed' '[1]: substring: the range 3 to 2 is out of the string (the length is 5)' '' '(substring "hello" 3 2)'
check_heap 'string-ref range' '[1]: string-ref: index 5 is out of range (the length is 5)' '' '(string-ref "hello" 5)'
check_heap 'string compare mixed' '[1]: < takes only 2 numbers or 2 strings' '' '(< "a" 1)'

# String functions of the library
LIB='(load "examples/library.lisp") '
run 'string->list' '(a b c)' "$LIB"'(string->list "abc")'
run 'string-search' 3 "$LIB"'(string-search "lo" "hello" 3)'
run 'string-search none' '()' "$LIB"'(string-search "zz" "hello" 0)'
run 'string-split' '(a b  c)' "$LIB"'(string-split "a,b,,c" ",")'
run 'string-split long sep' '(a b c)' "$LIB"'(string-split "a--b--c" "--")'
run 'string-split none' '(abc)' "$LIB"'(string-split "abc" ",")'
run 'string-join' 'x, y, z' "$LIB"'(string-join (list "x" "y" "z") ", ")'
run 'string-join empty' '' "$LIB"'(string-join () ", ")'
run 'string-trim' 'hi there' "$LIB"'(string-trim "  hi there \n")'
run 'string-trim blank' '' "$LIB"'(string-trim "   ")'
run 'string-prefix?' t "$LIB"'(string-prefix? "he" "hello")'
run 'string-suffix?' '()' "$LIB"'(string-suffix? "hello!" "hello")'
run 'number->string' 3.5 "$LIB"'(number->string 3.5)'

# Input and output
TMPF=$(mktemp)
trap 'rm -f "$TMPF"' EXIT
check_io 'args' '(a b c)' '' '(args)' a 'b c'
check_io 'args count' 2 '' '(length (args))' a 'b c'
check_io 'args none' '()' '' '(args)'
check_io 'read-line' abc 'abc\ndef\n' '(read-line)'
check_io 'read-line twice' def 'abc\ndef\n' '(read-line) (read-line)'
check_io 'read-line eof' '()' 'abc\n' '(read-line) (read-line)'
check_io 'read-line no end of line' 'last' 'last' '(read-line)'
check_io 'read-line empty line' 0 '\n' '(length (read-line))'
check_io 'read-line crlf' 2 'ab\r\n' '(length (read-line))'
check_io 'read-line loop' 3 'a\nb\nc\n' '
  (define n 0)
  (while (read-line) (setq n (+ n 1)))
  n'
check_io 'write-file, read-file' 'hello' '' "(write-file \"$TMPF\" \"hello\") (read-file \"$TMPF\")"
check_io 'write-file replaces' 'new' '' "(write-file \"$TMPF\" \"old content\") (write-file \"$TMPF\" \"new\") (read-file \"$TMPF\")"
check_io 'append-file' 7 '' "(write-file \"$TMPF\" \"hello\\n\") (append-file \"$TMPF\" \"x\") (length (read-file \"$TMPF\"))"
check_io 'read-file empty' 0 '' "(write-file \"$TMPF\" \"\") (length (read-file \"$TMPF\"))"
check_io 'read-file large' 20000 '' "(define s \"0123456789\") (write-file \"$TMPF\" (string-concat s s)) (define i 0) (while (< i 999) (append-file \"$TMPF\" (string-concat s s)) (setq i (+ i 1))) (length (read-file \"$TMPF\"))"
check_io 'file-exists?' t '' "(file-exists? \"$TMPF\")"
check_io 'file-exists? not' '()' '' '(file-exists? "/nonexistent/file")'
check_io 'read-file missing' '[1]: read-file: cannot open /nonexistent/file: No such file or directory' '' '(read-file "/nonexistent/file")'
check_io 'write-file bad dir' '[1]: write-file: cannot open /nonexistent/file: No such file or directory' '' '(write-file "/nonexistent/file" "x")'
check_io 'read-file bad argument' '[1]: read-file takes a file name' '' '(read-file 1)'
check_io 'write-file bad argument' '[1]: write-file takes a file name and a string' '' '(write-file "f")'

# Lexical closures
run closure 3 '(defun call (f) ((lambda (var) (f)) 5))
  ((lambda (var) (call (lambda () var))) 3)'

run counter 3 '
  (define counter
    ((lambda (val)
       (lambda () (setq val (+ val 1)) val))
     0))
  (counter)
  (counter)
  (counter)'

run print 'hello world 3()' '(print "hello" "world" 3)'
run println '()' '(println)'

run progn 'I own 10 cents()' '(progn (print "I own ") 
                              (defun add(x y)(+ x y))
                              (print (add 3 7)) 
                              (print " cents"))'

# While loop
run while 45 "
  (define i 0)
  (define sum 0)
  (while (< i 10)
    (setq sum (+ sum i))
    (setq i (+ i 1)))
  sum"

# Macros
run macro 42 "
  (defun list (x . y) (cons x y))
  (defmacro if-zero (x then) (list 'if (list '= x 0) then))
  (if-zero 0 42)"

run macro 7 '(defmacro seven () 7) ((lambda () (seven)))'

run macroexpand '(if (= x 0) (print x))' "
  (defun list (x . y) (cons x y))
  (defmacro if-zero (x then) (list 'if (list '= x 0) then))
  (macroexpand (if-zero x (print x)))"


# Tail calls must not consume stack: 20000 nested calls would overflow it otherwise
run tailcall 200010000 '(defun sum (n acc) (if (= n 0) acc (sum (- n 1) (+ n acc)))) (sum 20000 0)'
run tailcall 200010000 '(defun sum (n acc) (progn 1 (if (= n 0) acc (progn 2 (sum (- n 1) (+ n acc)))))) (sum 20000 0)'
run tailcall 200010000 '
  (defmacro my-if (c a b) (list (quote if) c a b))
  (defun sum (n acc) (my-if (= n 0) acc (sum (- n 1) (+ n acc))))
  (sum 20000 0)'
run tailcall t '
  (defun even? (n) (if (= n 0) t (odd? (- n 1))))
  (defun odd? (n) (if (= n 0) () (even? (- n 1))))
  (even? 20000)'
run tailcall 0 '(defun f (n) ((lambda (m) (if (= m 0) 0 (f (- m 1)))) n)) (f 20000)'

# and, or
run and t '(and)'
run and 3 '(and 1 2 3)'
run and '()' '(and 1 () 3)'
run and '()' '(and () (car 1))'
run or '()' '(or)'
run or 2 '(or () 2 (car 1))'
run or '()' '(or () ())'
run or a "(or () 'a 'b)"
run and 0 '(defun f (n) (and (> n -1) (if (= n 0) 0 (f (- n 1))))) (f 20000)'
run or t '(defun f (n) (or (= n 0) (f (- n 1)))) (f 20000)'

# Sum from 0 to 10
run recursion 55 '(defun f (x) (if (= x 0) 0 (+ (f (+ x -1)) x))) (f 10)'
# Floats
run float 1.5 '1.5'
run float -2.25 '-2.25'
run float 1000.0 '1e3'
run float 0.0015 '1.5e-3'
run float 200.0 '2E+2'
run float 1e+21 '1e21'
run float 0.30000000000000004 '(+ 0.1 0.2)'
run float 3.5 '(+ 1 2.5)'
run float 1.0 '(* 2 0.5)'
run float -3.5 '(- 3.5)'
run float 3.5 '(/ 7 2.0)'
run float 3 '(/ 7 2)'
run float 1.5 '(mod 7.5 2)'
run float '(1.5 2 (3.0))' "(list 1.5 2 '(3.0))"
run float 'x=1.25 y=3' '(string-concat "x=" 1.25 " y=" 3)'
run float 3.0 '(float 3)'
run float -2 '(truncate -2.7)'
run float -3 '(floor -2.7)'
run float 3 '(round 2.5)'
run float 5 '(truncate 5)'
run float 1.4142135623730951 '(sqrt 2)'
run float 1024.0 '(expt 2 10)'
run float 1.0 '(exp 0)'
run float 0.0 '(log 1)'
run float 0.0 '(sin 0)'
run float 1.0 '(cos 0)'
run float 'inf' '(* 1e300 1e300)'
run float t '(float? 1.0)'
run float '()' '(float? 1)'
run float t '(integer? 1)'
run float '()' '(integer? 1.0)'
run float 2.5 '(define x 2.5) x'
run float 10000.0 '(defun f (n x) (if (= n 0) x (f (- n 1) (+ x 1.0)))) (f 10000 0)'
run '< float' t '(< 1 1.5)'
run '< float' '()' '(< 2.5 2)'
run '>= float' t '(>= 2.0 2)'
run '= float' t '(= 1 1.0)'
run '= float' '()' '(= 1.5 2)'
run eq t '(eq 1.5 1.5)'
run eq '()' '(eq 1 1.0)'
run eq '()' '(eq 1.5 2.5)'

# Vectors
run vector '[1 2 3]' '[1 2 3]'
run vector '[]' '[]'
run vector '[3 a 1.5 [4 5]]' '[(+ 1 2) "a" 1.5 [4 5]]'
run vector '[1 2]' '(vector 1 (+ 1 1))'
run vector '[0 0 0]' '(make-vector 3 0)'
run vector '[() ()]' '(make-vector 2)'
run vector '(1 2 3)' '(vector->list [1 2 3])'
run vector '[a b c]' "(list->vector '(a b c))"
run vector 3 '(length [1 2 3])'
run vector 0 '(length [])'
run vector t '(vector? [1])'
run vector '()' "(vector? '(1))"
run vector t '(atom [1])'
run aref b "(aref ['a 'b 'c] 1)"
run aref 3 '(aref [1 [2 3]] 1) (aref (aref [1 [2 3]] 1) 1)'
run aset '[0 x 0]' "(define v (make-vector 3 0)) (aset v 1 'x) v"
run aset 9 '(define v (vector 1 2)) (aset v 0 9)'
run aset 9 '(define v (vector 1 2)) (aset v 0 9) (aref v 0)'
run aset '[0 0 0]' '(defun f () [0 0 0]) (aset (f) 0 9) (f)'
run eq t '(define v [1]) (eq v v)'
run eq '()' '(eq [1] [1])'
run vector 398 '(define n 200) (define v (make-vector n 1)) (define i 0) (while (< i n) (aset v i (* i 2)) (setq i (+ i 1))) (aref v (- n 1))'
run vector '[(1 2) [3]]' "(vector '(1 2) [3])"

# Size of the heap: -m is the initial size and -M the maximum, in KB.
# Robustness: the inputs which used to crash the interpreter
check_heap 'min integer' -9223372036854775808 '' '-9223372036854775808'
check_heap 'max integer' 9223372036854775807 '' '9223372036854775807'
check_heap 'integer too large' '[1]: Integer too large' '' '9223372036854775808'
check_heap 'integer too small' '[1]: Integer too large' '' '-9223372036854775809'
check_heap 'many quotes' '[1]: Stack overflow: the recursion is too deep' '' "$(printf "'%.0s" $(seq 100000))a"
check_heap 'deeply nested parentheses' '[1]: Stack overflow: the recursion is too deep' '' "$(printf '(%.0s' $(seq 100000))"
check_err 'print deep list' '[1]: Stack overflow: the recursion is too deep' '(define x ()) (define i 0) (while (< i 300000) (setq x (list x)) (setq i (+ i 1))) (print x)'
check_err 'print cyclic list' '[1]: Stack overflow: the recursion is too deep' '(define x (list 1 2)) (setcar x x) (print x)'
check_heap 'macro as the head of a form' 1 '' '(defmacro a () 1) (defmacro c () (list a)) (c)'
check_heap 'heap fixed' 3 '-m 32 -M 32' '(length (list 1 2 3))'
check_heap 'heap garbage' 50000 '-m 32 -M 32' '(define i 0) (while (< i 50000) (setq i (+ i 1))) i'
check_heap 'heap exhausted' '[1]: Memory exhausted' '-m 32 -M 32' '(make-vector 5000 0)'
check_heap 'heap grows' 5000 '-m 32 -M 100' '(length (make-vector 5000 0))'
check_heap 'heap max is reached' '[1]: Memory exhausted' '-m 32 -M 100' '(make-vector 13000 0)'
check_heap 'heap max clips the default' 3000 '-M 48' '(length (make-vector 3000 0))'
check_heap 'heap long options' 3 '--heap=64 --max-heap=64' '(+ 1 2)'
check_heap 'heap too small' 'minilisp: the heap size must be at least 32 KB' '-m 31' '1'
check_heap 'heap not a number' "minilisp: invalid size for the heap (-m): '3x' (a number of KB is expected)" '-m 3x' '1'
check_heap 'heap larger than max' 'minilisp: the initial heap size (-m) is larger than the maximum (-M)' '-m 64 -M 32' '1'
