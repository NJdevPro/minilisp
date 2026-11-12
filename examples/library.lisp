;;
;;  Simple library of useful functions and macros
;; 

(defun cadr (x) (car (cdr x)))
(defun cddr (x) (cdr (cdr x)))
(defun caddr (x) (car (cdr (cdr x))))

(defun max (a b) (if (> a b) a b))
(defun abs (n) (if (< n 0) (- n) n))
(defun gcd (a b) (if (= b 0) (abs a) (gcd b (mod a b))))

;; Is e the form (op ...) ?
(defun op? (e op) (and (not (atom e)) (eq (car e) op)))

(defun memq (x lst)
  (and lst (or (eq x (car lst)) (memq x (cdr lst)))))

;; (let var val body ...)
;; => ((lambda (var) body ...) val)
(defmacro let (var val . body)
  (cons (cons 'lambda (cons (list var) body))
	(list val)))

;; (when expr body ...)
;; => (if expr (progn body ...))
(defmacro when (expr . body)
  (cons 'if (cons expr (list (cons 'progn body)))))

;; (unless expr body ...)
;; => (if expr () body ...)
(defmacro unless (expr . body)
  (cons 'if (cons expr (cons () body))))

;;;
;;; List operators
;;;

;;; Applies each element of lis to fn, and returns their return values as a list.
(defun map (lis fn)
  (when lis
    (cons (fn (car lis))
	  (map (cdr lis) fn))))

(defun reduce (fn lst init)
  (if (eq () lst)
      init
      (reduce fn 
              (cdr lst)
              (fn init (car lst)))))
              
;; Applies each element of lis to pred. If pred returns a true value, terminate
;; the evaluation and returns pred's return value. If all of them return (),
;; returns ().
(defun any (lis pred)
  (when lis
    (or (pred (car lis))
	      (any (cdr lis) pred))))

;; returns t if elem exists in list l
(defun member (l elem) 
  (any l (lambda (x) (or (eq x elem) (= x elem)))))

;; Returns nth element of lis.
(defun nth (lis n)
  (if (= n 0)
      (car lis)
      (nth (cdr lis) (- n 1))))

;; Returns the nth tail of lis.
(defun nth-tail (lis n)
  (if (= n 0)
      lis
      (nth-tail (cdr lis) (- n 1))))

;; Returns a list consists of m .. n-1 integers.
(defun %iota (m n)
  (unless (<= n m)
    (cons m (%iota (+ m 1) n))))

;; Returns a list consists of 0 ... n-1 integers.
(defun iota (n) (%iota 0 n))

;; Returns a new list whose length is len and all members are init.
(defun make-list (len init)
  (unless (= len 0)
    (cons init (make-list (- len 1) init))))

;; Applies fn to each element of lis.
(defun for-each (lis fn)
  (or (not lis)
      (progn (fn (car lis))
	     (for-each (cdr lis) fn))))

; Concatenates and flattens lists into a single list
(defun append (first . rest)
  (if (eq () rest)
      first
      (append2 first 
              (append-reduce rest))))

(defun append2 (x y)
  (if (eq () x)
      y
      (cons (car x) 
            (append2 (cdr x) y))))

(defun append-reduce (lists)
  (if (eq () (cdr lists))
      (car lists)
      (append2 (car lists)
              (append-reduce (cdr lists)))))

(defun filter (pred lst)
  (if (eq () lst)
      ()
      (if (pred (car lst))
          (cons (car lst) 
                (filter pred (cdr lst)))
          (filter pred (cdr lst)))))

(defun quicksort (lst)
  (if (eq () lst)
      ()
      (if (eq () (cdr lst))
          lst
          (let pivot (car lst)
            (append 
              (quicksort (filter (lambda(x) (< x pivot)) (cdr lst)))
              (cons pivot ())
              (quicksort (filter (lambda(x) (>= x pivot)) (cdr lst)))) ))))
            

;;;
;;; String operators
;;;
;;; A character is a string of length 1, as returned by string-ref. The primitives are
;;; length, substring, string-ref, string-concat, list->string, string->number and the
;;; comparisons (eq and < <= > >= work on strings): the operators below are built on them.
;;; Strings are made of bytes: these functions know nothing about UTF-8.
;;;

;; The string of the number n
(defun number->string (n) (string-concat n))

;; The list of the characters of s
(defun string->list (s) (%string->list s (- (length s) 1) ()))

(defun %string->list (s i acc)
  (if (< i 0)
      acc
      (%string->list s (- i 1) (cons (string-ref s i) acc))))

;; The index of the first occurrence of the string needle in s, at the index start or
;; after it, or () if there is none.
(defun string-search (needle s start)
  (%string-search needle s start (length needle) (- (length s) (length needle))))

(defun %string-search (needle s i n last)
  (if (> i last)
      ()
      (if (eq (substring s i (+ i n)) needle)
          i
          (%string-search needle s (+ i 1) n last))))

;; Does the string s start (or end) with the string affix?
(defun string-prefix? (affix s)
  (and (<= (length affix) (length s))
       (eq (substring s 0 (length affix)) affix)))

(defun string-suffix? (affix s)
  (and (<= (length affix) (length s))
       (eq (substring s (- (length s) (length affix))) affix)))

;; The list of the parts of s which are separated by the string sep: "a,b" -> ("a" "b")
(defun string-split (s sep)
  (if (= (length sep) 0)
      (error "string-split: the separator is empty")
      (%string-split s sep (length sep) 0 ())))

(defun %string-split (s sep n start acc)
  (let i (string-search sep s start)
    (if i
        (%string-split s sep n (+ i n) (cons (substring s start i) acc))
        (reverse (cons (substring s start) acc)))))

;; Concatenates the strings of the list lst, with the string sep between them.
(defun string-join (lst sep) (list->string (%interleave lst sep ())))

(defun %interleave (lst sep acc)
  (if (eq () lst)
      (reverse acc)
      (%interleave (cdr lst) sep
                   (if acc
                       (cons (car lst) (cons sep acc))
                       (cons (car lst) acc)))))

;; Removes the spaces, tabs and new lines at both ends of s
(defun string-trim (s)
  (let i (%skip-left s 0)
    (substring s i (%skip-right s (length s) i))))

(defun %space? (c) (or (eq c " ") (eq c "\t") (eq c "\n") (eq c "\r")))

(defun %skip-left (s i)
  (if (and (< i (length s)) (%space? (string-ref s i)))
      (%skip-left s (+ i 1))
      i))

(defun %skip-right (s j start)
  (if (and (> j start) (%space? (string-ref s (- j 1))))
      (%skip-right s (- j 1) start)
      j))
