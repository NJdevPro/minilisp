(defun list (x . y) (cons x y))

(defun hanoi-print (disk from to)
  (println (string-concat "Move disk " disk 
    " from " (symbol->string from) 
    " to " (symbol->string to) )))

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