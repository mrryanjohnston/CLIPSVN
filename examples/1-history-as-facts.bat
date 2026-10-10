; ======================================================================
; History as facts.
;
; A Subversion binding for CLIPS is not for a printed log. svn does that
; better. The purpose is that a revision can become a fact, and rules match
; facts. This example builds a small history, puts it into working memory,
; and lets the rule engine make the conclusions.
;
;   CLIPSVN -f2 examples/1-history-as-facts.bat
; ======================================================================

; The slot names are the field names of the log records. A template needs
; only the fields that its rules use.
(deftemplate revision
  (slot revision)
  (slot author)
  (slot message)
  (slot changed-path-count))

; One fact for each path that a revision changed. copyfrom-path is nil unless
; the path was copied from somewhere: a branch, a tag, a rename.
(deftemplate change
  (slot revision)
  (slot action)
  (slot path)
  (slot copyfrom-path))

(defrule large-commit
  (revision (revision ?r) (message ?m) (changed-path-count ?n&:(>= ?n 3)))
  =>
  (println "r" ?r " touched " ?n " paths: " ?m))

(defrule a-copy
  (change (revision ?r) (action A) (path ?to) (copyfrom-path ?from&~nil))
  =>
  (println "r" ?r " copied " ?from " to " ?to))

(defrule a-deletion
  (change (revision ?r) (action D) (path ?p))
  =>
  (println "r" ?r " removed " ?p))

(defrule prolific-author
  (revision (author ?who))
  (not (counted ?who))
  =>
  (bind ?n 0)
  (do-for-all-facts ((?r revision)) (eq ?r:author ?who) (bind ?n (+ ?n 1)))
  (assert (counted ?who))
  (if (>= ?n 2) then (println ?who " made " ?n " commits")))

; ----------------------------------------------------------------------
; Build a history: a trunk, three files, a change by a second person, a
; branch, and a deletion. The username of the context is the author.
; ----------------------------------------------------------------------

(defglobal ?*nl* = (format nil "%n"))

(deffunction write-file (?path ?content)
  (open ?path f "w")
  (printout f ?content ?*nl*)
  (close f))

(deffunction build-history ()
  (system "rm -rf examples/tmp && mkdir -p examples/tmp")
  (bind ?repos (svn-repos-create "examples/tmp/repos"))
  (bind ?url (svn-repos-file-url ?repos))
  (svn-repos-free ?repos)
  (bind ?ctx (svn-client-create-context "examples/tmp/config"))

  (svn-client-set-auth ?ctx "ada")
  (svn-client-mkdir ?ctx (str-cat ?url "/trunk") FALSE "make the trunk")
  (svn-client-checkout ?ctx (str-cat ?url "/trunk") "examples/tmp/work")
  (write-file "examples/tmp/work/notes.txt" "first draft")
  (write-file "examples/tmp/work/README" "read me")
  (write-file "examples/tmp/work/old.txt" "to be removed")
  (svn-client-add ?ctx "examples/tmp/work/notes.txt")
  (svn-client-add ?ctx "examples/tmp/work/README")
  (svn-client-add ?ctx "examples/tmp/work/old.txt")
  (svn-client-commit ?ctx "examples/tmp/work" "start the project")

  (svn-client-set-auth ?ctx "grace")
  (write-file "examples/tmp/work/notes.txt" "second draft")
  (svn-client-commit ?ctx "examples/tmp/work" "revise the notes")

  (svn-client-set-auth ?ctx "ada")
  (svn-client-copy ?ctx (str-cat ?url "/trunk") (str-cat ?url "/branches/work") HEAD FALSE TRUE "branch for the work")

  (svn-client-set-auth ?ctx "grace")
  (svn-client-delete ?ctx "examples/tmp/work/old.txt")
  (svn-client-commit ?ctx "examples/tmp/work" "remove the old file")

  (svn-client-set-auth ?ctx "ada")
  (write-file "examples/tmp/work/README" "read me first")
  (svn-client-commit ?ctx "examples/tmp/work" "improve the readme")

  (create$ ?ctx ?url))

; ----------------------------------------------------------------------
; Put the history into working memory. Every revision becomes a revision
; fact, and every path that a revision changed becomes a change fact.
; ----------------------------------------------------------------------

(deffunction load-history (?ctx ?url)
  (bind ?log (svn-client-log ?ctx ?url))
  (bind ?revisions (svn-records-to-facts ?log revision))
  (bind ?changes (svn-records-to-facts (svn-log-changed-paths ?log) change))
  (svn-records-free ?log)
  (println "loaded " ?revisions " revisions and " ?changes " changed paths"))

(deffunction main ()
  (bind ?built (build-history))
  (bind ?ctx (nth$ 1 ?built))
  (load-history ?ctx (nth$ 2 ?built))
  (svn-client-context-free ?ctx)
  (run)
  (system "rm -rf examples/tmp")
  (println "done"))

(main)
(exit)
