;;
;;  Sudoku solver, with vectors
;;
;;  The same algorithm as sudoku.lisp (backtracking which always fills the empty
;;  cell that has the fewest candidates), but the grid and the sets of used digits
;;  are vectors instead of lists: reading or writing a cell takes the same time
;;  whatever its position, where a list has to be walked through.
;;
;;  Run it with:  ./minilisp examples/sudoku2.lisp
;;
;;  The cells are numbered from 0 to 80, row by row. The digits already used in
;;  a row, a column or a box are kept as one integer, the sum of 2^(d-1) for
;;  each digit d: MiniLisp has no bitwise operators, but dividing by 2^(d-1)
;;  and taking the remainder modulo 2 tells whether digit d is in the set.
;;

(load "library.lisp")

;;; ------------------------------------------------------------------
;;; State of the search
;;; ------------------------------------------------------------------

(define grid ())    ; the 81 cells, 0 for an empty one
(define rows ())    ; the 9 sets of digits used in each row
(define cols ())    ; ... in each column
(define boxes ())   ; ... in each 3x3 box
(define nodes 0)    ; number of digits tried so far

(define pow2 [1 2 4 8 16 32 64 128 256])

(defun box-of (r c) (+ (* (/ r 3) 3) (/ c 3)))

;; Adds delta to the nth element of the vector vec
(defun bump (vec n delta)
  (aset vec n (+ (aref vec n) delta)))

;; Puts the digit d in the cell i, and updates the sets of used digits.
;; delta is 2^(d-1) to add the digit, and -2^(d-1) to remove it.
(defun set-cell (i d delta)
  ((lambda (r c)
     (aset grid i d)
     (bump rows r delta)
     (bump cols c delta)
     (bump boxes (box-of r c) delta))
   (/ i 9) (mod i 9)))

(defun place (i d)   (set-cell i d (aref pow2 (- d 1))))
(defun unplace (i d) (set-cell i 0 (- (aref pow2 (- d 1)))))

;;; ------------------------------------------------------------------
;;; Candidates
;;; ------------------------------------------------------------------

;; Is the digit whose weight is p absent from the three sets?
(defun free? (rs cs bs p)
  (and (= 0 (mod (/ rs p) 2))
       (= 0 (mod (/ cs p) 2))
       (= 0 (mod (/ bs p) 2))))

;; The digits from d down to 1 which are free, added in front of acc.
;; p is the weight of d.
(defun cands (rs cs bs d p acc)
  (if (= d 0)
      acc
      (cands rs cs bs (- d 1) (/ p 2)
             (if (free? rs cs bs p) (cons d acc) acc))))

;; The list of the digits which can be put in the cell i
(defun cands-of (i)
  ((lambda (r c)
     (cands (aref rows r) (aref cols c) (aref boxes (box-of r c)) 9 256 ()))
   (/ i 9) (mod i 9)))

;; Looks for the empty cell with the fewest candidates among the cells from i
;; on. Returns (index . candidates), or () if there is no empty cell.
(defun best (i best-i best-c best-n)
  (if (= i 81)
      (if (< best-i 0) () (cons best-i best-c))
      (if (= (aref grid i) 0)
          ((lambda (c)
             ((lambda (n)
                (if (< n best-n)
                    (if (<= n 1)
                        (cons i c)                ; no need to look further
                        (best (+ i 1) i c n))
                    (best (+ i 1) best-i best-c best-n)))
              (length c)))
           (cands-of i))
          (best (+ i 1) best-i best-c best-n))))

;;; ------------------------------------------------------------------
;;; Search
;;; ------------------------------------------------------------------

;; Returns t if the grid can be completed. The grid then holds the solution.
(defun solve ()
  ((lambda (b)
     (or (eq () b)
         (try (car b) (cdr b))))
   (best 0 -1 () 10)))

;; Tries the candidates cs one after the other in the cell i
(defun try (i cs)
  (if (eq () cs)
      ()
      (progn
        (setq nodes (+ nodes 1))
        (place i (car cs))
        (or (solve)
            (progn
              (unplace i (car cs))
              (try i (cdr cs)))))))

;;; ------------------------------------------------------------------
;;; Input / output
;;; ------------------------------------------------------------------

;; Puts the given digits of the vector givens in the grid
(defun load-givens (givens i)
  (if (< i 81)
      (progn
        (or (= (aref givens i) 0) (place i (aref givens i)))
        (load-givens givens (+ i 1)))))

;; puzzle is a list of 9 rows of 9 digits, 0 being an empty cell
(defun init (puzzle)
  (setq grid (make-vector 81 0))
  (setq rows (make-vector 9 0))
  (setq cols (make-vector 9 0))
  (setq boxes (make-vector 9 0))
  (setq nodes 0)
  (load-givens (list->vector (append-reduce puzzle)) 0))

;; The string of the n cells from the cell i on, with a space between the groups of 3
(defun row-str (i n acc)
  (if (= n 0)
      acc
      (row-str (+ i 1) (- n 1)
               (string-concat acc
                              (if (= (aref grid i) 0) "." (aref grid i))
                              (if (and (> n 1) (= (mod n 3) 1)) " " "")))))

(defun show-rows (r)
  (if (< r 9)
      (progn
        (println (row-str (* r 9) 9 ""))
        (if (and (< r 8) (= (mod r 3) 2)) (println "") ())
        (show-rows (+ r 1)))))

(defun solve-puzzle (name puzzle)
  (println name)
  (init puzzle)
  (show-rows 0)
  (println "")
  (if (solve)
      (progn
        (println (string-concat "Solved after trying " nodes " digits:"))
        (show-rows 0))
      (println "No solution"))
  (println ""))

;;; ------------------------------------------------------------------
;;; Puzzles
;;; ------------------------------------------------------------------

(define easy '((5 3 0  0 7 0  0 0 0)
               (6 0 0  1 9 5  0 0 0)
               (0 9 8  0 0 0  0 6 0)

               (8 0 0  0 6 0  0 0 3)
               (4 0 0  8 0 3  0 0 1)
               (7 0 0  0 2 0  0 0 6)

               (0 6 0  0 0 0  2 8 0)
               (0 0 0  4 1 9  0 0 5)
               (0 0 0  0 8 0  0 7 9)))

;; Said to be the hardest sudoku in the world (Arto Inkala, 2012)
(define hard '((8 0 0  0 0 0  0 0 0)
               (0 0 3  6 0 0  0 0 0)
               (0 7 0  0 9 0  2 0 0)

               (0 5 0  0 0 7  0 0 0)
               (0 0 0  0 4 5  7 0 0)
               (0 0 0  1 0 0  0 3 0)

               (0 0 1  0 0 0  0 6 8)
               (0 0 8  5 0 0  0 1 0)
               (0 9 0  0 0 0  4 0 0)))

;; The last cell of the first row can only be a 9, but there is already a 9
;; below it: there is no solution
(define impossible '((1 2 3  4 5 6  7 8 0)
                     (0 0 0  0 0 0  0 0 9)
                     (0 0 0  0 0 0  0 0 0)

                     (0 0 0  0 0 0  0 0 0)
                     (0 0 0  0 0 0  0 0 0)
                     (0 0 0  0 0 0  0 0 0)

                     (0 0 0  0 0 0  0 0 0)
                     (0 0 0  0 0 0  0 0 0)
                     (0 0 0  0 0 0  0 0 0)))

(solve-puzzle "Easy puzzle" easy)
(solve-puzzle "Hardest puzzle" hard)
(solve-puzzle "Impossible puzzle" impossible)
