;;
;;  Symbolic calculator
;;
;;  Works on formulas of one variable, written as Lisp lists:
;;
;;      (+ a b ...)  (- a b ...)  (* a b ...)  (/ a b)  (expt a n)  (sqrt a)
;;      (sin a) (cos a) (tan a) (exp a) (log a)
;;
;;  Numbers are integers, floats, or exact fractions written (/ n d). Any symbol
;;  other than the variable is a constant, and pi is 3.14159...
;;
;;      (simplify e)        puts a formula in a canonical form
;;      (expand e)          multiplies out the products and the integer powers
;;      (diff e x)          derivative of e with respect to x
;;      (taylor e x a n)    Taylor polynomial of e around x = a, up to degree n
;;      (evaluate e x v)    numerical value of e for x = v
;;      (show e)            the formula as a string, in the usual notation
;;
;;  The calculations are exact as long as the numbers are integers or fractions
;;  (they must fit in 64 bits, so a Taylor polynomial is limited to degree 20).
;;
;;  Run it with:  ./minilisp examples/symbolic.lisp
;;
;;  How it works: SIMPLIFY gives every formula one shape: only sums, products and
;;  powers (a - b is a + (-1)*b and a / b is a * b^-1), whose terms and factors are
;;  sorted and gathered (x + x gives 2*x, x * x gives x^2, constants are
;;  folded). Two equal formulas have then the same form, which is what makes
;;  DIFF, EXPAND and TAYLOR short: they build their result in any shape and call
;;  SIMPLIFY.
;;

(load "library.lisp")

;;; ------------------------------------------------------------------
;;; Small helpers
;;; ------------------------------------------------------------------

;; Splices the forms (op ...) of args into the list: (+ a (+ b c)) -> a b c
(defun flatten-op (op args)
  (if (eq args ())
      ()
      (append (if (op? (car args) op) (cdr (car args)) (list (car args)))
              (flatten-op op (cdr args)))))

;; Sorts a list. less is true if its first argument must come before the second.
(defun insert-by (x lst less)
  (if (eq lst ())
      (list x)
      (if (less x (car lst))
          (cons x lst)
          (cons (car lst) (insert-by x (cdr lst) less)))))

(defun sort-by (lst less)
  (reduce (lambda (acc x) (insert-by x acc less)) lst ()))

(defun join (strs sep)
  (if (eq strs ())
      ""
      (reduce (lambda (acc s) (string-concat acc sep s)) (cdr strs) (car strs))))

;;; ------------------------------------------------------------------
;;; Numbers: integers, floats and fractions (/ n d)
;;; ------------------------------------------------------------------

(defun ratio? (e)
  (and (op? e '/) (integer? (cadr e)) (integer? (caddr e))))

(defun num? (e) (or (integer? e) (float? e) (ratio? e)))

;; Is e the integer or the float n?
(defun num-eq? (e n) (and (or (integer? e) (float? e)) (= e n)))
(defun zero? (e) (num-eq? e 0))
(defun one? (e) (num-eq? e 1))

(defun numer (x) (if (ratio? x) (cadr x) x))
(defun denom (x) (if (ratio? x) (caddr x) 1))

(defun num->float (x)
  (if (ratio? x) (/ (float (cadr x)) (caddr x)) (float x)))

(defun neg-num? (x) (and (num? x) (< (num->float x) 0)))

;; The fraction n/d in lowest terms: an integer if d divides n
(defun mk-ratio (n d)
  (if (= d 0)
      (error "Division by zero")
      (let g (gcd n d)
        (let s (if (< d 0) -1 1)
          (let n2 (* s (/ n g))
            (let d2 (* s (/ d g))
              (if (= d2 1) n2 (list '/ n2 d2))))))))

(defun num-add (a b)
  (if (or (float? a) (float? b))
      (+ (num->float a) (num->float b))
      (mk-ratio (+ (* (numer a) (denom b)) (* (numer b) (denom a)))
                (* (denom a) (denom b)))))

(defun num-mul (a b)
  (if (or (float? a) (float? b))
      (* (num->float a) (num->float b))
      (mk-ratio (* (numer a) (numer b)) (* (denom a) (denom b)))))

(defun num-div (a b)
  (if (zero? b)
      (error "Division by zero")
      (if (or (float? a) (float? b))
          (/ (num->float a) (num->float b))
          (mk-ratio (* (numer a) (denom b)) (* (denom a) (numer b))))))

(defun num-neg (x) (num-mul -1 x))
(defun num-abs (x) (if (neg-num? x) (num-neg x) x))

;; b to the integer power n
(defun num-pow (b n)
  (if (< n 0)
      (num-div 1 (num-pow b (- n)))
      (if (= n 0) 1 (num-mul b (num-pow b (- n 1))))))

;;; ------------------------------------------------------------------
;;; A total order on formulas, to sort and compare them
;;; ------------------------------------------------------------------

;; The symbols are ordered by their position here, the unknown ones being added at the end
(define sym-table '(x y z a b c n pi + * expt sin cos tan exp log))

(defun index-in (lst s i)
  (if (eq lst ())
      -1
      (if (eq (car lst) s) i (index-in (cdr lst) s (+ i 1)))))

(defun sym-index (s)
  (let i (index-in sym-table s 0)
    (if (< i 0)
        (progn (setq sym-table (append sym-table (list s)))
               (- (length sym-table) 1))
        i)))

;; 0 for the numbers, 1 for the symbols, 2 for the lists
(defun rank (e) (if (num? e) 0 (if (atom e) 1 2)))

(defun compare (a b)
  (let ra (rank a)
    (let rb (rank b)
      (if (< ra rb) -1
        (if (> ra rb) 1
          (if (= ra 0) (compare-nums a b)
            (if (= ra 1) (compare-ints (sym-index a) (sym-index b))
                (compare-lists a b))))))))

(defun compare-ints (x y) (if (< x y) -1 (if (> x y) 1 0)))
(defun compare-nums (a b) (compare-ints (num->float a) (num->float b)))

(defun compare-lists (a b)
  (if (eq a ())
      (if (eq b ()) 0 -1)
      (if (eq b ())
          1
          (let c (compare (car a) (car b))
            (if (= c 0) (compare-lists (cdr a) (cdr b)) c)))))

;; Roughly the degree of a formula in the variable: it orders the terms of a sum
;; from the constants up to the functions
(defun degree (e)
  (if (num? e) 0
    (if (atom e) 1
      (if (op? e '+) (reduce (lambda (m x) (max m (degree x))) (cdr e) (degree (cadr e)))
        (if (op? e '*) (reduce (lambda (s f) (+ s (degree f))) (cdr e) 0)
          (if (and (op? e 'expt) (num? (caddr e)))
              (* (num->float (caddr e)) (degree (cadr e)))
              100))))))

;;; ------------------------------------------------------------------
;;; Simplification
;;; ------------------------------------------------------------------

(defun mk-prod (fs)
  (if (eq fs ()) 1 (if (eq (cdr fs) ()) (car fs) (cons '* fs))))

(defun mk-sum (ts)
  (if (eq ts ()) 0 (if (eq (cdr ts) ()) (car ts) (cons '+ ts))))

;; A term of a sum is a number, or a product which starts with a number, or
;; something else. Its coefficient is the number (1 if there is none), and the
;; rest is what is left once the number is removed.
(defun term-coef (tm)
  (if (num? tm) tm (if (and (op? tm '*) (num? (cadr tm))) (cadr tm) 1)))

(defun term-rest (tm)
  (if (num? tm) 1 (if (and (op? tm '*) (num? (cadr tm))) (mk-prod (cddr tm)) tm)))

(defun mk-term (coef rest)
  (if (zero? coef) 0
    (if (one? rest) coef
      (if (one? coef) rest
        (if (op? rest '*) (cons '* (cons coef (cdr rest))) (list '* coef rest))))))

;; acc is a list of (rest coef): adds coef * rest to it
(defun add-term (coef rest acc)
  (if (eq acc ())
      (list (list rest coef))
      (if (= 0 (compare rest (car (car acc))))
          (cons (list rest (num-add coef (cadr (car acc)))) (cdr acc))
          (cons (car acc) (add-term coef rest (cdr acc))))))

(defun collect-terms (args acc)
  (if (eq args ())
      acc
      (collect-terms (cdr args)
                     (add-term (term-coef (car args)) (term-rest (car args)) acc))))

(defun term< (p q)
  (let dp (degree (car p))
    (let dq (degree (car q))
      (if (< dp dq) t (if (> dp dq) () (< (compare (car p) (car q)) 0))))))

;; The sum of simplified formulas
(defun simp-sum (args)
  (let terms (collect-terms (flatten-op '+ args) ())
    (mk-sum (map (sort-by (filter (lambda (p) (not (zero? (cadr p)))) terms) term<)
                 (lambda (p) (mk-term (cadr p) (car p)))))))

(defun split-pow (f) (if (op? f 'expt) (list (cadr f) (caddr f)) (list f 1)))

;; acc is a list of (base exponent): adds base^exponent to it
(defun add-power (b e acc)
  (if (eq acc ())
      (list (list b e))
      (if (= 0 (compare b (car (car acc))))
          (cons (list b (simp-sum (list e (cadr (car acc))))) (cdr acc))
          (cons (car acc) (add-power b e (cdr acc))))))

(defun collect-powers (fs acc)
  (if (eq fs ())
      acc
      (collect-powers (cdr fs)
                      (add-power (car (split-pow (car fs))) (cadr (split-pow (car fs))) acc))))

;; The product of simplified formulas
(defun simp-prod (args)
  (let fs (flatten-op '* args)
    (let c (reduce (lambda (acc f) (if (num? f) (num-mul acc f) acc)) fs 1)
      (if (zero? c)
          0
          (let pairs (sort-by (collect-powers (filter (lambda (f) (not (num? f))) fs) ())
                              (lambda (p q) (< (compare (car p) (car q)) 0)))
            (let powers (map pairs (lambda (p) (simp-pow (car p) (cadr p))))
              (if (any powers (lambda (f) (or (num? f) (op? f '*))))
                  ;; Some powers became numbers or products: gather again
                  (simp-prod (cons c powers))
                  (if (one? c) (mk-prod powers) (cons '* (cons c powers))))))))))

;; b to the power n, both simplified
(defun simp-pow (b n)
  (if (zero? n) 1
    (if (one? n) b
      (if (one? b) 1
        (if (and (num? b) (num? n))
            (if (or (float? b) (float? n))
                (expt (num->float b) (num->float n))
                (if (integer? n) (num-pow b n) (list 'expt b n)))
            (if (and (op? b 'expt) (integer? n))
                (simp-pow (cadr b) (simp-prod (list (caddr b) n)))
                (if (and (op? b '*) (integer? n))
                    (simp-prod (map (cdr b) (lambda (f) (simp-pow f n))))
                    (list 'expt b n))))))))

;; A function of a simplified formula
(defun simp-fn (op a)
  (if (not (memq op '(sin cos tan exp log)))
      (error "Unknown function " (symbol->string op))
      (if (float? a)
          (apply-fn op a)
          (if (and (eq op 'sin) (zero? a)) 0
            (if (and (eq op 'cos) (zero? a)) 1
              (if (and (eq op 'tan) (zero? a)) 0
                (if (and (eq op 'exp) (zero? a)) 1
                  (if (and (eq op 'log) (one? a)) 0
                    (if (and (eq op 'exp) (op? a 'log)) (cadr a)
                      (if (and (eq op 'log) (op? a 'exp)) (cadr a)
                          (list op a)))))))))))

(defun apply-fn (op x)
  (if (eq op 'sin) (sin x)
    (if (eq op 'cos) (cos x)
      (if (eq op 'tan) (/ (sin x) (cos x))
        (if (eq op 'exp) (exp x)
            (log x))))))

(defun simplify (e)
  (if (ratio? e)
      (mk-ratio (cadr e) (caddr e))
      (if (atom e)
          e
          (let op (car e)
            (let args (map (cdr e) simplify)
              (if (eq op '+) (simp-sum args)
                (if (eq op '*) (simp-prod args)
                  (if (eq op '-)
                      (if (eq (cdr args) ())
                          (simp-prod (list -1 (car args)))
                          (simp-sum (cons (car args)
                                          (map (cdr args) (lambda (x) (simp-prod (list -1 x)))))))
                    (if (eq op '/) (simp-prod (list (car args) (simp-pow (cadr args) -1)))
                      (if (eq op 'expt) (simp-pow (car args) (cadr args))
                        (if (eq op 'sqrt) (simp-pow (car args) (list '/ 1 2))
                            (simp-fn op (car args)))))))))))))

;;; ------------------------------------------------------------------
;;; Expansion
;;; ------------------------------------------------------------------

;; The product of two expanded formulas, distributed over the sums
(defun mul-ex (a b)
  (if (op? a '+)
      (simp-sum (map (cdr a) (lambda (x) (mul-ex x b))))
      (if (op? b '+)
          (simp-sum (map (cdr b) (lambda (x) (mul-ex a x))))
          (simp-prod (list a b)))))

;; b^n, b being expanded
(defun pow-ex (b n)
  (if (and (integer? n) (> n 1) (op? b '+))
      (mul-ex b (pow-ex b (- n 1)))
      (simp-pow b n)))

(defun ex (e)
  (if (or (num? e) (atom e))
      e
      (let op (car e)
        (if (eq op '+) (simp-sum (map (cdr e) ex))
          (if (eq op '*) (reduce mul-ex (map (cdr e) ex) 1)
            (if (eq op 'expt) (pow-ex (ex (cadr e)) (caddr e))
                (simp-fn op (ex (cadr e)))))))))

(defun expand (e) (ex (simplify e)))

;;; ------------------------------------------------------------------
;;; Differentiation
;;; ------------------------------------------------------------------

;; The derivative of the factors fs multiplied together
(defun deriv-prod (fs v)
  (if (eq (cdr fs) ())
      (deriv (car fs) v)
      (list '+ (list '* (deriv (car fs) v) (cons '* (cdr fs)))
               (list '* (car fs) (deriv-prod (cdr fs) v)))))

;; (a^b)' is b a^(b-1) a' if b is a number, a^b (b' log(a) + b a'/a) otherwise
(defun deriv-pow (a b v)
  (if (num? b)
      (list '* b (list 'expt a (list '- b 1)) (deriv a v))
      (list '* (list 'expt a b)
            (list '+ (list '* (deriv b v) (list 'log a))
                     (list '* b (deriv a v) (list 'expt a -1))))))

;; f(u)' is f'(u) u', where du is u'
(defun deriv-fn (op u du)
  (if (eq op 'sin) (list '* (list 'cos u) du)
    (if (eq op 'cos) (list '* -1 (list 'sin u) du)
      (if (eq op 'tan) (list '* (list '+ 1 (list 'expt (list 'tan u) 2)) du)
        (if (eq op 'exp) (list '* (list 'exp u) du)
          (if (eq op 'log) (list '* (list 'expt u -1) du)
              (error "Cannot differentiate the function " (symbol->string op))))))))

;; The derivative of a simplified formula, in any shape
(defun deriv (e v)
  (if (num? e) 0
    (if (atom e) (if (eq e v) 1 0)
      (let op (car e)
        (if (eq op '+) (cons '+ (map (cdr e) (lambda (x) (deriv x v))))
          (if (eq op '*) (deriv-prod (cdr e) v)
            (if (eq op 'expt) (deriv-pow (cadr e) (caddr e) v)
                (deriv-fn op (cadr e) (deriv (cadr e) v)))))))))

(defun diff (e v) (simplify (deriv (simplify e) v)))

;;; ------------------------------------------------------------------
;;; Taylor polynomials
;;; ------------------------------------------------------------------

;; Replaces the symbol v by the formula a in e
(defun subst-var (e v a)
  (if (eq e v)
      a
      (if (atom e)
          e
          (cons (car e) (map (cdr e) (lambda (x) (subst-var x v a)))))))

;; d is the kth derivative of e, and fact is k!. acc holds the terms so far.
(defun taylor-loop (d v a n k fact acc)
  (if (> k n)
      (expand (cons '+ acc))
      (taylor-loop (expand (deriv d v)) v a n (+ k 1) (* fact (+ k 1))
                   (cons (list '* (list '/ (subst-var d v a) fact)
                                  (list 'expt (list '- v a) k))
                         acc))))

(defun taylor (e v a n)
  (if (or (< n 0) (> n 20))
      (error "The degree of a Taylor polynomial must be between 0 and 20")
      (taylor-loop (simplify e) v a n 0 1 ())))

;;; ------------------------------------------------------------------
;;; Numerical evaluation
;;; ------------------------------------------------------------------

(define pi-value 3.141592653589793)

(defun evaluate (e v val)
  (ev e v (float val)))

(defun ev (e v val)
  (if (num? e) (num->float e)
    (if (atom e)
        (if (eq e v) val
          (if (eq e 'pi) pi-value
              (error "No value for the symbol " (symbol->string e))))
        (let op (car e)
          (let xs (map (cdr e) (lambda (x) (ev x v val)))
            (if (eq op '+) (reduce (lambda (a b) (+ a b)) xs 0.0)
              (if (eq op '*) (reduce (lambda (a b) (* a b)) xs 1.0)
                (if (eq op '-) (if (eq (cdr xs) ())
                                   (- (car xs))
                                   (reduce (lambda (a b) (- a b)) (cdr xs) (car xs)))
                  (if (eq op '/) (reduce (lambda (a b) (/ a b)) (cdr xs) (car xs))
                    (if (eq op 'expt) (expt (car xs) (cadr xs))
                      (if (eq op 'sqrt) (sqrt (car xs))
                          (if (memq op '(sin cos tan exp log))
                              (apply-fn op (car xs))
                              (error "Unknown function " (symbol->string op))))))))))))))

;;; ------------------------------------------------------------------
;;; Display, in the usual notation: 1 + x - x^3/6
;;; ------------------------------------------------------------------

(defun paren (s) (string-concat "(" s ")"))

(defun show-num (x)
  (if (ratio? x) (string-concat (cadr x) "/" (caddr x)) (string-concat x)))

;; The string of e. p is the precedence of the context: 1 for a sum, 2 for a
;; product, 3 for a power. e is put between parentheses if it binds less.
(defun show-prec (e p)
  (if (num? e)
      (let s (show-num e)
        (if (or (and (ratio? e) (>= p 2)) (and (neg-num? e) (>= p 3))) (paren s) s))
      (if (atom e)
          (symbol->string e)
          (let op (car e)
            (if (eq op '+)
                (let s (show-sum (cdr e)) (if (> p 1) (paren s) s))
                (if (or (eq op '*) (and (eq op 'expt) (neg-num? (caddr e))))
                    (let s (show-prod e) (if (> p 2) (paren s) s))
                    (if (eq op 'expt)
                        (let s (string-concat (show-prec (cadr e) 4) "^" (show-prec (caddr e) 4))
                          (if (> p 3) (paren s) s))
                        (string-concat (symbol->string op) "(" (show-prec (cadr e) 0) ")"))))))))

;; A negative term is shown after a minus, as its opposite
(defun show-sum (ts)
  (join (cons (show-term (car ts) "" "-") (map (cdr ts) (lambda (tm) (show-term tm " + " " - ")))) ""))

(defun show-term (tm plus minus)
  (if (neg-num? (term-coef tm))
      (string-concat minus (show-prec (simp-prod (list -1 tm)) 1))
      (string-concat plus (show-prec tm 1))))

(defun prod-factors (e)
  (if (op? e '*) (if (num? (cadr e)) (cddr e) (cdr e)) (list e)))

(defun neg-power? (f) (and (op? f 'expt) (neg-num? (caddr f))))

;; A product is shown as a fraction: the factors with a negative exponent, and the
;; denominator of the coefficient, go below the bar.
(defun show-prod (e)
  (let c (term-coef e)
    (let fs (prod-factors e)
      (let up (filter (lambda (f) (not (neg-power? f))) fs)
        (let down (map (filter neg-power? fs)
                       (lambda (f) (simp-pow (cadr f) (num-neg (caddr f)))))
          (let nc (numer (num-abs c))
            (let dc (denom (num-abs c))
              (let top (append (if (and (one? nc) up) () (list (show-num nc)))
                               (map up (lambda (f) (show-prec f 3))))
                (let bottom (append (if (one? dc) () (list (show-num dc)))
                                    (map down (lambda (f) (show-prec f 3))))
                  (string-concat (if (neg-num? c) "-" "")
                                 (join top "*")
                                 (if bottom
                                     (string-concat "/" (if (cdr bottom)
                                                            (paren (join bottom "*"))
                                                            (car bottom)))
                                     "")))))))))))

(defun show (e) (show-prec (simplify e) 0))

;;; ------------------------------------------------------------------
;;; Demonstration
;;; ------------------------------------------------------------------

;; Prints the formula, the operation applied to it and the result
(defun try (title e result)
  (println title e)
  (println "    =" (show result))
  (println ""))

(println "--- simplify")
(try "simplify" '(+ x x (* 2 x) 3 -3) (simplify '(+ x x (* 2 x) 3 -3)))
(try "simplify" '(/ (* x (expt x 2)) (* 2 x)) (simplify '(/ (* x (expt x 2)) (* 2 x))))
(try "simplify" '(+ (/ 1 3) (/ 1 6)) (simplify '(+ (/ 1 3) (/ 1 6))))
(try "simplify" '(- (exp (log (+ x 1))) 1) (simplify '(- (exp (log (+ x 1))) 1)))

(println "--- expand")
(try "expand" '(expt (+ x 1) 3) (expand '(expt (+ x 1) 3)))
(try "expand" '(* (+ x 1) (- x 1)) (expand '(* (+ x 1) (- x 1))))
(try "expand" '(expt (- (* 2 x) 3) 2) (expand '(expt (- (* 2 x) 3) 2)))
(try "expand" '(* (sin (* (+ x 1) 2)) (+ x 1)) (expand '(* (sin (* (+ x 1) 2)) (+ x 1))))

(println "--- diff")
(try "d/dx" '(expt x 5) (diff '(expt x 5) 'x))
(try "d/dx" '(* x (sin x)) (diff '(* x (sin x)) 'x))
(try "d/dx" '(/ 1 x) (diff '(/ 1 x) 'x))
(try "d/dx" '(/ (sin x) x) (diff '(/ (sin x) x) 'x))
(try "d/dx" '(exp (* 2 x)) (diff '(exp (* 2 x)) 'x))
(try "d/dx" '(log (cos x)) (diff '(log (cos x)) 'x))
(try "d/dx" '(tan x) (diff '(tan x) 'x))
(try "d/dx" '(sqrt (+ 1 (expt x 2))) (diff '(sqrt (+ 1 (expt x 2))) 'x))
(try "d/dx" '(expt x x) (diff '(expt x x) 'x))
(try "d3/dx3" '(sin (* 2 x)) (diff (diff (diff '(sin (* 2 x)) 'x) 'x) 'x))

(println "--- taylor")
(try "taylor (around 0, degree 5)" '(exp x) (taylor '(exp x) 'x 0 5))
(try "taylor (around 0, degree 7)" '(sin x) (taylor '(sin x) 'x 0 7))
(try "taylor (around 0, degree 6)" '(cos x) (taylor '(cos x) 'x 0 6))
(try "taylor (around 0, degree 7)" '(tan x) (taylor '(tan x) 'x 0 7))
(try "taylor (around 0, degree 5)" '(log (+ 1 x)) (taylor '(log (+ 1 x)) 'x 0 5))
(try "taylor (around 0, degree 5)" '(/ 1 (- 1 x)) (taylor '(/ 1 (- 1 x)) 'x 0 5))
(try "taylor (around 0, degree 4)" '(sqrt (+ 1 x)) (taylor '(sqrt (+ 1 x)) 'x 0 4))
(try "taylor (around 1, degree 3)" '(log x) (taylor '(log x) 'x 1 3))

(println "--- evaluate")
(println "(sin (/ pi 6)) =" (evaluate '(sin (/ pi 6)) 'x 0))
(println "(+ (expt x 2) 1) for x = 3:" (evaluate '(+ (expt x 2) 1) 'x 3))
(println "sin(x) for x = 0.5:" (evaluate '(sin x) 'x 0.5))
(println "the Taylor polynomial of degree 7:" (evaluate (taylor '(sin x) 'x 0 7) 'x 0.5))
(println "the difference:"
         (abs (- (evaluate '(sin x) 'x 0.5) (evaluate (taylor '(sin x) 'x 0 7) 'x 0.5))))
