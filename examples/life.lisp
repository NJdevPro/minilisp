;;;
;;; Conway's game of life
;;;

(load "examples/library.lisp")

(define width 10)
(define height 10)

;; Returns location (x, y)'s element.
(defun get (board x y)
  (nth (nth board y) x))

;; Returns true if location (x, y)'s value is "@".
(defun alive? (board x y)
  (and (<= 0 x)
       (< x height)
       (<= 0 y)
       (< y width)
       (eq (get board x y) '@)))

;; Print out the given board.
(defun print (board)
  (if (not board)
      '$
    (println (car board))
    (print (cdr board))))

(defun count (board x y)
  (let at (lambda (x y)
            (if (alive? board x y) 1 0))
       (+ (at (- x 1) (- y 1))
          (at (- x 1) y)
          (at (- x 1) (+ y 1))
          (at x (- y 1))
          (at x (+ y 1))
          (at (+ x 1) (- y 1))
          (at (+ x 1) y)
          (at (+ x 1) (+ y 1)))))

(defun next (board x y)
  (let c (count board x y)
       (if (alive? board x y)
           (or (= c 2) (= c 3))
         (= c 3))))

(defun run (board)
  (while t
    (print board)
    (println '*)
    (let newboard (map (iota height)
                       (lambda (y)
                         (map (iota width)
                              (lambda (x)
                                (if (next board x y) '@ '_)))))
         (setq board newboard))))

(run '((_ _ _ _ _ _ _ _ _ _)
       (_ _ _ _ _ _ _ _ _ _)
       (_ _ _ _ _ _ _ _ _ _)
       (_ _ _ _ _ _ _ _ _ _)
       (_ _ _ _ _ _ _ _ _ _)
       (_ _ _ _ _ _ _ _ _ _)
       (_ _ _ _ _ _ _ _ _ _)
       (_ @ @ @ _ _ _ _ _ _)
       (_ _ _ @ _ _ _ _ _ _)
       (_ _ @ _ _ _ _ _ _ _)))
