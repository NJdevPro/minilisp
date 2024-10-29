;(defun list (x . y) (cons x y))

;(defun list2 (a b) (cons a (cons b ())))
;(defun list3 (a b c) (cons a (cons b (cons c ()))))

(defmacro cond (rest)
  (if (= () rest) 
      ()
      (if (= (car (car rest)) t)
          (car (cdr (car rest)))
          (list 'if 
                (car (car rest))
                (car (cdr (car rest)))
                (cond (cdr rest))))))

(defun mapc1 (fn xs)
  (if (= () xs)
      ()
      (progn 
        (fn (car xs)) 
        (mapc1 fn (cdr xs)))))

(defun hanoi-print (disk from to)
  (println (cons 'Move 
           (cons 'disk 
           (cons disk
           (cons 'from
           (cons from
           (cons 'to
           (cons to ())))))))))

(defun hanoi-move (n from to via)
  (if (= n 1)
      (hanoi-print n from to)
      (progn
        (hanoi-move (- n 1) from via to)
        (hanoi-print n from to)
        (hanoi-move (- n 1) via to from))))

(defun hanoi (n)
  (hanoi-move n 'L 'M 'R))

(hanoi 5)