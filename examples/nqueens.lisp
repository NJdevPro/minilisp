;;;
;;; N-queens puzzle solver.
;;;
;;; The N queens puzzle is the problem of placing N chess queens on an N x N
;;; chessboard so that no two queens attack each
;;; other. http://en.wikipedia.org/wiki/Eight_queens_puzzle
;;;
;;; This program solves N-queens puzzle by depth-first backtracking.
;;;

(load "examples/library.lisp")

;;;
;;; N-queens solver
;;;

;; Creates size x size list filled with symbol "x".
(defun make-board (size)
  (map (iota size)
       (lambda (_)
	 (make-list size 'x))))

;; Returns location (x, y)'s element.
(defun get (board x y)
  (nth (nth board x) y))

;; Set symbol "Q" to location (x, y).
(defun set (board x y)
  (setcar (nth-tail (nth board x) y) 'Q))

;; Set symbol "x" to location (x, y).
(defun clear (board x y)
  (setcar (nth-tail (nth board x) y) 'x))

;; Returns true if location (x, y)'s value is "Q".
(defun set? (board x y)
  (eq (get board x y) 'Q))

;; Print out the given board.
(defun print (board)
  (if (not board)
      '$
    (println (car board))
    (print (cdr board))))

;; Returns true if we cannot place a queen at position (x, y), assuming that
;; queens have already been placed on each row from 0 to x-1.
(defun conflict? (board x y)
  (any (iota x)
       (lambda (n)
	 (or
	  ;; Check if there's no conflicting queen upward
	  (set? board n y)
	  ;; Upper left
	  (let z (+ y (- n x))
		(and (<= 0 z)
		     (set? board n z)))
	  ;; Upper right
	  (let z (+ y (- x n))
		(and (< z board-size)
		     (set? board n z)))))))

;; Find positions where we can place queens at row x, and continue searching for
;; the next row.
(defun %solve (board x)
  (if (= x board-size)
      ;; Problem solved
      (progn (print board)
	     (println '$))
    (map (iota board-size)
	      (lambda (y)
		(unless (conflict? board x y)
		  (set board x y)
		  (%solve board (+ x 1))
		  (clear board x y))))))

(defun solve (board)
  (println 'start)
  (%solve board 0)
  (println 'done))

;;;
;;; Main
;;;

(define board-size 8)
(define board (make-board board-size))
(solve board)
