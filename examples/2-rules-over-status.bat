; ======================================================================
; Rules over the working copy.
;
; svn status is a list of paths, with the state of each path. As facts, rules
; can reason about those states: which changes are safe to commit on their
; own, which need a review, and what to do about each. Here the rules decide,
; and the rule that decides to commit makes the commit.
;
;   CLIPSVN -f2 examples/2-rules-over-status.bat
; ======================================================================

(deftemplate change
  (slot path)
  (slot node-status))

(deftemplate decision
  (slot path)
  (slot what))

; A new file under docs is safe to add and commit as it is.
(defrule new-documentation
  (change (path ?p&:(eq (str-index "docs/" ?p) 1)) (node-status svn_wc_status_unversioned))
  =>
  (assert (decision (path ?p) (what add))))

; A modified source file is committed.
(defrule modified-source
  (change (path ?p&:(eq (sub-string (- (str-length ?p) 1) (str-length ?p) ?p) ".c"))
          (node-status svn_wc_status_modified))
  =>
  (assert (decision (path ?p) (what commit))))

; A file that is missing from disk needs a decision from a person.
(defrule missing-file
  (change (path ?p) (node-status svn_wc_status_missing))
  =>
  (assert (decision (path ?p) (what ask))))

; Other unversioned files are left alone. The lower salience lets the rules
; above decide first.
(defrule unversioned-elsewhere
  (declare (salience -5))
  (change (path ?p) (node-status svn_wc_status_unversioned))
  (not (decision (path ?p)))
  =>
  (assert (decision (path ?p) (what leave))))

(defrule report
  (declare (salience -10))
  (decision (path ?p) (what ?w))
  =>
  (println ?w ": " ?p))

; When every decision is made, the files marked add are added, and they are
; committed together with the files marked commit.
(defrule commit-what-was-approved
  (declare (salience -20))
  (work ?ctx ?wc)
  (exists (decision (what add|commit)))
  =>
  (bind ?paths (create$))
  (do-for-all-facts ((?d decision)) (eq ?d:what add)
    (svn-client-add ?ctx (str-cat ?wc "/" ?d:path)))
  (do-for-all-facts ((?d decision)) (or (eq ?d:what add) (eq ?d:what commit))
    (bind ?paths (create$ ?paths (str-cat ?wc "/" ?d:path))))
  (bind ?rev (svn-client-commit ?ctx ?paths "changes the rules approved"))
  (println "committed r" ?rev " with " (length$ ?paths) " approved changes"))

; ----------------------------------------------------------------------
; A working copy with one revision, and then some changes to it.
; ----------------------------------------------------------------------

(defglobal ?*nl* = (format nil "%n"))

(deffunction write-file (?path ?content)
  (open ?path f "w")
  (printout f ?content ?*nl*)
  (close f))

(deffunction make-working-copy ()
  (system "rm -rf examples/tmp && mkdir -p examples/tmp")
  (bind ?repos (svn-repos-create "examples/tmp/repos"))
  (bind ?url (svn-repos-file-url ?repos))
  (svn-repos-free ?repos)
  (bind ?ctx (svn-client-create-context "examples/tmp/config"))
  (svn-client-set-auth ?ctx "the-rules")
  (svn-client-checkout ?ctx ?url "examples/tmp/work")

  (svn-client-mkdir ?ctx (create$ "examples/tmp/work/src" "examples/tmp/work/docs"))
  (write-file "examples/tmp/work/src/main.c" "int main(void) { return 0; }")
  (write-file "examples/tmp/work/src/old.c" "/* nothing */")
  (svn-client-add ?ctx "examples/tmp/work/src/main.c")
  (svn-client-add ?ctx "examples/tmp/work/src/old.c")
  (svn-client-commit ?ctx "examples/tmp/work" "first")

  ; Now the changes: a modified source file, a new document, a file that
  ; was removed without telling Subversion, and an unexpected file at the
  ; top level.
  (write-file "examples/tmp/work/src/main.c" "int main(void) { return 1; }")
  (write-file "examples/tmp/work/docs/guide.md" "# Guide")
  (system "rm examples/tmp/work/src/old.c")
  (write-file "examples/tmp/work/scratch.txt" "temporary")
  ?ctx)

; Every status record becomes a change fact.
(deffunction load-status (?ctx ?wc)
  (bind ?status (svn-client-status ?ctx ?wc))
  (bind ?n (svn-records-to-facts ?status change))
  (svn-records-free ?status)
  ?n)

(deffunction main ()
  (bind ?ctx (make-working-copy))
  (assert (work ?ctx "examples/tmp/work"))
  (println (load-status ?ctx "examples/tmp/work") " changes in the working copy")
  (run)
  (bind ?status (svn-client-status ?ctx "examples/tmp/work"))
  (println (svn-records-count ?status) " left uncommitted")
  (svn-records-free ?status)
  (svn-client-context-free ?ctx)
  (system "rm -rf examples/tmp")
  (println "done"))

(main)
(exit)
