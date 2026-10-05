(defpackage #:nixbox-sbcl
  (:use #:cl)
  (:export #:run))
(in-package #:nixbox-sbcl)

(defmacro twice (form) `(+ ,form ,form))
(defclass box () ((value :initarg :value :reader box-value)))
(defclass bonus-box (box) ())
(defgeneric score (box))
(defmethod score ((box box)) (* 3 (box-value box)))
(defmethod score ((box bonus-box)) (+ 11 (call-next-method)))

(sb-alien:define-alien-routine ("sb_opendir" c-opendir) sb-alien:system-area-pointer
  (name sb-alien:c-string))
(sb-alien:define-alien-routine ("sb_readdir" c-readdir) sb-alien:system-area-pointer
  (dir sb-alien:system-area-pointer))
(sb-alien:define-alien-routine ("sb_closedir" c-closedir) sb-alien:int
  (dir sb-alien:system-area-pointer))
(sb-alien:define-alien-routine ("sb_dirent_name" c-dirent-name) sb-alien:c-string
  (entry sb-alien:system-area-pointer))
(sb-alien:define-alien-routine ("_errno" c-errno) (* sb-alien:int))

(defun native-directory (path)
  (let ((dir (c-opendir (namestring path))))
    (assert (not (zerop (sb-sys:sap-int dir))))
    (unwind-protect
         (loop for entry = (c-readdir dir)
               until (zerop (sb-sys:sap-int entry))
               for name = (c-dirent-name entry)
               unless (member name '("." "..") :test #'string=)
                 collect name into names
               finally (assert (zerop (sb-alien:deref (c-errno))))
                       (return (sort names #'string<)))
      (assert (zerop (c-closedir dir))))))

(defun run (report-path)
  (let* ((failures 0)
         (checks 0)
         (root (make-pathname :name nil :type nil :defaults (pathname report-path)))
         (populated (merge-pathnames "fixtures/populated/" root))
         (empty (merge-pathnames "fixtures/empty/" root)))
    (with-open-file (report report-path :direction :output :if-exists :supersede
                                        :external-format :utf-8)
      (format report "SBCL ~A / ~A ~A~%" (lisp-implementation-version)
              (machine-type) (software-type))
      (finish-output report)
      (flet ((check (name function)
               (format report "RUN  ~A~%" name)
               (finish-output report)
               (incf checks)
               (handler-case
                   (progn (funcall function) (format report "PASS ~A~%" name))
                 (error (condition)
                   (incf failures)
                   (format report "FAIL ~A: ~A~%" name condition)))
               (finish-output report)))
        (check "Windows Lisp core booted"
               (lambda () (assert (member :win32 *features*))))
        (check "Reader, evaluation and exact bignums: 30!"
               (lambda ()
                 (assert (= (eval (read-from-string "(+ (* -7 9) 4)")) -59))
                 (assert (= (loop for n from 1 to 30 for product = n then (* product n)
                                 finally (return product))
                            265252859812191058636308480000000))))
        (check "Native compiler and macro expansion"
               (lambda ()
                 (let ((fn (compile nil '(lambda (x y) (twice (- (* x 3) y))))))
                   (assert (compiled-function-p fn))
                   (assert (= (funcall fn -7 5) -52))
                   (assert (= (funcall fn 9 -4) 62)))))
        (check "Compiled closures capture their environment"
               (lambda ()
                 (let* ((factory (compile nil '(lambda (n) (lambda (x) (+ (* x n) 7)))))
                        (fn (funcall factory 13)))
                   (assert (= (funcall fn -2) -19))
                   (assert (= (funcall fn 5) 72)))))
        (check "Static CRT floating-point foreign calls"
               (lambda ()
                 (assert (< (abs (- (sin (/ pi 2)) 1d0)) 1d-12))
                 (assert (< (abs (- (log 8d0 2d0) 3d0)) 1d-12))
                 (assert (< (abs (- (exp (log 17d0)) 17d0)) 1d-11))))
        (check "CLOS subclass dispatch and CALL-NEXT-METHOD"
               (lambda ()
                 (assert (= (score (make-instance 'box :value 7)) 21))
                 (assert (= (score (make-instance 'bonus-box :value -4)) -1))))
        (check "Conditions, type errors and UNWIND-PROTECT"
               (lambda ()
                 (let ((cleaned nil))
                   (assert (eq :caught
                               (handler-case
                                   (unwind-protect (error "intentional condition")
                                     (setf cleaned t))
                                 (simple-error () :caught))))
                   (assert cleaned))
                 (let ((fn (compile nil '(lambda (x) (declare (optimize (safety 3)))
                                           (car x)))))
                   (assert (handler-case (progn (funcall fn 37) nil)
                             (type-error () t))))))
        (check "Full GC preserves 50,000 live cons pairs"
               (lambda ()
                 (let ((pairs (loop for i below 50000 collect (cons i (- (* i 3) 7)))))
                   (dotimes (round 4)
                     (loop repeat 100000 collect (make-array 8 :initial-element round))
                     (sb-ext:gc :full t)
                     (assert (= (reduce #'+ pairs :key #'cdr) 3749575000)))
                   (assert (= (caar pairs) 0))
                   (assert (= (caar (last pairs)) 49999)))))
        (check "Two Lisp threads, mutexes and safepoints"
               (lambda ()
                 (let* ((counter 0)
                        (lock (sb-thread:make-mutex :name "probe counter"))
                        (workers
                          (loop repeat 2 collect
                            (sb-thread:make-thread
                             (lambda ()
                               (dotimes (i 20000)
                                 (sb-thread:with-mutex (lock) (incf counter)))
                               :done)))))
                   (dotimes (i 4) (sb-ext:gc :full t))
                   (dolist (worker workers)
                     (assert (eq :done (sb-thread:join-thread worker :timeout 30))))
                   (assert (= counter 40000)))))
        (check "Unicode file round-trip in writable app storage"
               (lambda ()
                 (let ((path (merge-pathnames "unicode-λ-雪.txt" root))
                       (text "Common Lisp on Xbox: λ 雪 🎮"))
                   (with-open-file (out path :direction :output :if-exists :supersede
                                             :external-format :utf-8)
                     (write-line text out))
                   (with-open-file (in path :external-format :utf-8)
                     (assert (string= (read-line in) text)))
                   (assert (delete-file path)))))
        (check "Compile a source file, load its FASL, execute it"
               (lambda ()
                 (let ((source (merge-pathnames "generated.lisp" root))
                       (fasl (merge-pathnames "generated.fasl" root)))
                   (with-open-file (out source :direction :output :if-exists :supersede)
                     (write-line "(in-package :nixbox-sbcl)" out)
                     (write-line "(defun fasl-generated (n) (- (* n 11) 3))" out))
                   (multiple-value-bind (path warnings failure)
                       (compile-file source :output-file fasl)
                     (declare (ignore warnings))
                     (assert (and path (not failure)))
                     (load path))
                   (assert (= (funcall 'fasl-generated -4) -47))
                   (assert (= (funcall 'fasl-generated 6) 63))
                   (delete-file source)
                   (delete-file fasl))))
        (check "MSVC directory wrappers: populated and empty"
               (lambda ()
                 (ensure-directories-exist populated)
                 (ensure-directories-exist empty)
                 (dolist (name '("alpha.txt" "beta.dat"))
                   (with-open-file (out (merge-pathnames name populated) :direction :output
                                                                             :if-exists :supersede)
                     (write-line name out)))
                 (assert (equal (native-directory populated) '("alpha.txt" "beta.dat")))
                 (assert (null (native-directory empty)))))
        (check "Directory wrappers reject missing paths and files"
               (lambda ()
                 (assert (zerop (sb-sys:sap-int
                                 (c-opendir (namestring (merge-pathnames "absent/" root))))))
                 (assert (= (sb-alien:deref (c-errno)) sb-unix::enoent))
                 (assert (zerop (sb-sys:sap-int
                                 (c-opendir (namestring (merge-pathnames "alpha.txt" populated))))))
                 (assert (= (sb-alien:deref (c-errno)) sb-unix::enotdir))))
        (check "MSVC stat-kind predicates distinguish files and directories"
               (lambda ()
                 (sb-alien:with-alien
                     ((isdir (function sb-alien:int sb-alien:unsigned-short)
                             :extern "s_isdir")
                      (isreg (function sb-alien:int sb-alien:unsigned-short)
                             :extern "s_isreg"))
                   (assert (= (sb-alien:alien-funcall isdir (logior sb-unix::s-ifdir #o644)) 1))
                   (assert (= (sb-alien:alien-funcall isdir sb-unix::s-ifreg) 0))
                   (assert (= (sb-alien:alien-funcall isreg sb-unix::s-ifreg) 1))
                   (assert (= (sb-alien:alien-funcall isreg sb-unix::s-ifdir) 0))))))
      (format report "COMPLETE: ~D checks, ~D failed~%" checks failures)
      (finish-output report))
    failures))
