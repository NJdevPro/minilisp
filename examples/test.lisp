(defun list (x . y) (cons x y))

(defun count-down (n)
   (if (= n 0)
       0
       (progn (println n)
              (count-down (- n 1)))))

(count-down 20)

(defun fact (n) 
  (if (= n 0)
    1
    (* n (fact (- n 1)))))

(fact 10)

