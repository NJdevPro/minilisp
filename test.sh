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

# strings
run 'string-concat' 'one & two and 3' '(string-concat "one" " & " "two" " and " 3)'
run 'symbol->string' 'twelve' "
  (define twelve 12)
  (symbol->string 'twelve)"
run 'string->symbol' 'twelve' '(string->symbol "twelve")'

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
