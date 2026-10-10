; ======================================================================
; Diff and blame.
;
; Which lines of a file did each person write, and what changed between two
; revisions? Both answers are lists of records here, and both lists become
; facts. The rules then say who owns the most of a file, and which paths a
; range of revisions touched.
;
;   CLIPSVN -f2 examples/3-diff-and-blame.bat
; ======================================================================

; one line of a file, from svn-client-blame
(deftemplate line
  (slot line-number)
  (slot revision)
  (slot author)
  (slot line))

; one path that differs between two revisions, from svn-client-diff-summarize
(deftemplate difference
  (slot path)
  (slot summarize-kind)
  (slot node-kind))

(defrule lines-by-author
  (line (author ?who))
  (not (counted ?who))
  =>
  (bind ?n 0)
  (do-for-all-facts ((?l line)) (eq ?l:author ?who) (bind ?n (+ ?n 1)))
  (assert (counted ?who))
  (println ?who " wrote " ?n " lines"))

(defrule latest-change-to-a-line
  (line (line-number ?n) (revision ?r) (author ?who) (line ?text))
  (not (line (revision ?later&:(> ?later ?r))))
  =>
  (println "line " ?n " is the newest: r" ?r " by " ?who ": " ?text))

(defrule added-path
  (difference (path ?p) (summarize-kind svn_client_diff_summarize_kind_added))
  =>
  (println "added: " ?p))

(defrule modified-path
  (difference (path ?p) (summarize-kind svn_client_diff_summarize_kind_modified))
  =>
  (println "modified: " ?p))

(defrule deleted-path
  (difference (path ?p) (summarize-kind svn_client_diff_summarize_kind_deleted))
  =>
  (println "deleted: " ?p))

; ----------------------------------------------------------------------
; A file that three people wrote, over three revisions, and a fourth revision
; that adds one file and removes another.
; ----------------------------------------------------------------------

(defglobal ?*nl* = (format nil "%n"))

(deffunction write-lines (?path $?lines)
  (open ?path f "w")
  (progn$ (?line ?lines) (printout f ?line ?*nl*))
  (close f))

(deffunction commit-as (?ctx ?who ?message)
  (svn-client-set-auth ?ctx ?who)
  (svn-client-commit ?ctx "examples/tmp/work" ?message))

(deffunction build-history ()
  (system "rm -rf examples/tmp && mkdir -p examples/tmp")
  (bind ?repos (svn-repos-create "examples/tmp/repos"))
  (bind ?url (svn-repos-file-url ?repos))
  (svn-repos-free ?repos)
  (bind ?ctx (svn-client-create-context "examples/tmp/config"))
  (svn-client-set-auth ?ctx "ada")
  (svn-client-checkout ?ctx ?url "examples/tmp/work")

  (write-lines "examples/tmp/work/poem.txt" "roses are red" "violets are blue")
  (write-lines "examples/tmp/work/draft.txt" "a draft")
  (svn-client-add ?ctx "examples/tmp/work/poem.txt")
  (svn-client-add ?ctx "examples/tmp/work/draft.txt")
  (commit-as ?ctx "ada" "the first two lines")

  (write-lines "examples/tmp/work/poem.txt" "roses are red" "violets are blue" "sugar is sweet")
  (commit-as ?ctx "grace" "a third line")

  (write-lines "examples/tmp/work/poem.txt" "roses are red" "violets are blue" "sugar is sweet" "and so are you")
  (commit-as ?ctx "linus" "the last line")

  (write-lines "examples/tmp/work/notes.txt" "about the poem")
  (svn-client-add ?ctx "examples/tmp/work/notes.txt")
  (svn-client-delete ?ctx "examples/tmp/work/draft.txt")
  (commit-as ?ctx "ada" "notes in, draft out")

  (create$ ?ctx ?url))

(deffunction main ()
  (bind ?built (build-history))
  (bind ?ctx (nth$ 1 ?built))
  (bind ?url (nth$ 2 ?built))

  ; who wrote each line of the poem, as it is in the repository now
  (bind ?blame (svn-client-blame ?ctx (str-cat ?url "/poem.txt")))
  (println "the poem has " (svn-records-to-facts ?blame line) " lines")
  (svn-records-free ?blame)

  ; what changed between the first revision and the last
  (bind ?summary (svn-client-diff-summarize ?ctx ?url 1 ?url 4))
  (println (svn-records-to-facts ?summary difference) " paths differ between r1 and r4")
  (svn-records-free ?summary)

  ; and the difference itself, as text
  (bind ?diff (svn-client-diff ?ctx (str-cat ?url "/poem.txt") 1 (str-cat ?url "/poem.txt") 4))
  (println "the diff of the poem adds the last line: "
           (if (str-index "+and so are you" ?diff) then yes else no))

  (svn-client-context-free ?ctx)
  (run)
  (system "rm -rf examples/tmp")
  (println "done"))

(main)
(exit)
