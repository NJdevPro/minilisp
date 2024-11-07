(defun list (x . y) (cons x y))

;; (let var val body ...)
;; => ((lambda (var) body ...) val)
(defmacro let (var val . body)
  (cons (cons 'lambda (cons (list var) body))
	(list val)))

;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; Control structures
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

(defmacro cond (rest)
  (if (= () rest) 
      ()
      (if (= (car (car rest)) t)
          (car (cdr (car rest)))
              (list 'if 
                (car (car rest))
                (car (cdr (car rest)))
                (cond (cdr rest))))))

;; (and e1 e2 ...)
;; => (if e1 (and e2 ...))
;; (and e1)
;; => e1
(defmacro and (expr . rest)
  (if rest
      (list 'if expr (cons 'and rest)) 
      expr))

;; (or e1 e2 ...)
;; => (let <tmp> e1
;;      (if <tmp> <tmp> (or e2 ...)))
;; (or e1)
;; => e1
;;
;; The reason to use the temporary variables is to avoid evaluating the
;; arguments more than once.
(defmacro or (expr . rest)
  (if rest
      (let var (gensym)
	    (list 'let var expr
		  (list 'if var var (cons 'or rest))))
    expr))

;; (when expr body ...)
;; => (if expr (progn body ...))
(defmacro when (expr . body)
  (cons 'if (cons expr (list (cons 'progn body)))))

;; (unless expr body ...)
;; => (if expr () body ...)
(defmacro unless (expr . body)
  (cons 'if (cons expr (cons () body))))



;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
;;; List operators
;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;

;; Applies each element of lis to pred. If pred returns a true value, terminate
;; the evaluation and returns pred's return value. If all of them return (),
;; returns ().
(defun any (lis pred)
  (when lis
    (or (pred (car lis))
	(any (cdr lis) pred))))

;;; Applies each element of lis to fn, and returns their return values as a list.
(defun map (lis fn)
  (when lis
    (cons (fn (car lis))
	  (map (cdr lis) fn))))

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
(defun iota (n)
  (%iota 0 n))

;; Returns a new list whose length is len and all members are init.
(defun make-list (len init)
  (unless (= len 0)
    (cons init (make-list (- len 1) init))))

;; Applies fn to each element of lis.
(defun for-each (lis fn)
  (or (not lis)
      (progn (fn (car lis))
	     (for-each (cdr lis) fn))))
         
