; ======================================================================
; CLIPSVN in-process test suite
;
; Every suite that this file batches shares one CLIPS environment, one
; process, and one assertion counter. The run ends with a non-zero exit status
; if an assertion failed.
;
; Two rules apply everywhere:
;
;   1. A call with arguments that are wrong for the wrapper goes through a
;      defglobal, never a literal. CLIPS checks literal arguments against
;      the AddUDF restriction string at parse time. So a literal does not
;      reach the guard of the wrapper, and the guard is what the test
;      checks.
;
;   2. Such a call is bound to a global on a line of its own, and the
;      assertion is on the next line. A rejected argument causes an
;      [ARGACCES2] evaluation error. An error inside an (expect ...) stops
;      the expect itself. The assertion would then disappear from the
;      count instead of a failure. The messages that the wrappers write
;      to stderr are the test, not noise.
;
; Each suite makes every repository and working copy that it uses under
; tests/tmp. The tests do not need an installed svn, a network, or one of
; your repositories.
; ======================================================================

(defglobal
  ?*tests-ran*    = 0
  ?*tests-failed* = 0)

(deffunction expect (?msg ?expected ?actual)
  (bind ?*tests-ran* (+ ?*tests-ran* 1))
  (if (eq ?expected ?actual)
   then
     (print ".")
   else
     (bind ?*tests-failed* (+ ?*tests-failed* 1))
     (println crlf "FAILURE: " ?msg
             crlf "  expected=" ?expected
             crlf "  actual=" ?actual)))

; For values that are real but not fixed: a uuid, a date, a path under
; tests/tmp. A check of the exact value would make the suite a record of this
; machine, not of the wrapper.
(deffunction expect-true (?msg ?actual)
  (expect ?msg TRUE (if ?actual then TRUE else FALSE)))

; A scratch directory for the repositories and working copies. tests/run.sh
; removes it at the start and at the end. Each suite removes what it is about
; to make. So a run by hand also cannot be affected by an earlier run.
(system "mkdir -p tests/tmp")

(batch* tests/constructs.bat)

; CLIPS strings do not read \n as a newline. So a newline is written here, and
; a suite adds it with str-cat where it needs one.
(defglobal ?*nl* = (format nil "%n"))

; CLIPS has no factp. This is the test for a fact address.
(deffunction factp (?x) (eq (type ?x) FACT-ADDRESS))
(deffunction handlep (?x) (eq (type ?x) EXTERNAL-ADDRESS))

; The Subversion that this binary was built against, in the numbers that this
; library uses for versions: 11405 is 1.14.5.
(defglobal ?*svn* = (svn-built-version))

; ----------------------------------------------------------------------
; One context for every suite. Its configuration directory is under
; tests/tmp, so the ~/.subversion of this machine plays no part. Its
; username is what the commits carry.
; ----------------------------------------------------------------------

(system "rm -rf tests/tmp/config")
(defglobal ?*ctx* = (svn-client-create-context "tests/tmp/config"))
(svn-client-set-auth ?*ctx* "tester")

; ----------------------------------------------------------------------
; The functions that every suite uses to get a repository, a working copy,
; and commits in them.
; ----------------------------------------------------------------------

; A new repository under tests/tmp. Returns the repository handle.
(deffunction make-repos (?name)
  (bind ?path (str-cat "tests/tmp/" ?name))
  (system (str-cat "rm -rf " ?path))
  (svn-repos-create ?path))

; The file:// URL of a repository made by make-repos.
(deffunction repos-url (?name)
  (svn-file-url-from-dirent (str-cat "tests/tmp/" ?name)))

; A working copy of a repository, checked out under tests/tmp. Returns the
; path of the working copy.
(deffunction make-wc (?repos-name ?wc-name)
  (bind ?path (str-cat "tests/tmp/" ?wc-name))
  (system (str-cat "rm -rf " ?path))
  (svn-client-checkout ?*ctx* (repos-url ?repos-name) ?path)
  ?path)

; Writes a file.
(deffunction write-file (?path ?content)
  (open ?path wf "w")
  (printout wf ?content)
  (close wf)
  ?path)

; Reads a file: the first line, or FALSE if the file is not there.
(deffunction read-line-of (?path)
  (if (open ?path rf "r")
   then
     (bind ?line (readline rf))
     (close rf)
     ?line
   else
     FALSE))

; Writes a file into a working copy, adds it if it is new, and commits. Returns
; the new revision.
(deffunction commit-file (?wc ?name ?content ?message)
  (bind ?path (str-cat ?wc "/" ?name))
  (bind ?new (not (open ?path probe "r")))
  (if (not ?new) then (close probe))
  (write-file ?path ?content)
  (if ?new then (svn-client-add ?*ctx* ?path))
  (svn-client-commit ?*ctx* ?wc ?message))

; The pre-revprop-change hook that lets a client change a revision property.
; Without it, a repository refuses every change to one.
(deffunction allow-revprop-changes (?repos)
  (bind ?hook (str-cat (svn-repos-hook-dir ?repos) "/pre-revprop-change"))
  (write-file ?hook (str-cat "#!/bin/sh" ?*nl* "exit 0" ?*nl*))
  (system (str-cat "chmod +x " ?hook)))

; The index of the first record whose field has a value, or FALSE.
(deffunction record-index (?records ?field ?value)
  (loop-for-count (?i 0 (- (svn-records-count ?records) 1))
    (if (eq (svn-record-field ?records ?i ?field) ?value) then (return ?i)))
  FALSE)

(println "CLIPSVN test suite: built for Subversion " ?*svn*
         ", running with " (svn-version))
(println)

(batch* tests/version-test-suite.bat)
(batch* tests/path-test-suite.bat)
(batch* tests/repos-test-suite.bat)
(batch* tests/context-test-suite.bat)
(batch* tests/wc-test-suite.bat)
(batch* tests/history-test-suite.bat)
(batch* tests/property-test-suite.bat)
(batch* tests/ra-test-suite.bat)
(batch* tests/records-test-suite.bat)
(batch* tests/handle-lifetime-test-suite.bat)
(batch* tests/arg-guard-test-suite.bat)

; ----------------------------------------------------------------------
; cleanup
;
; Each suite frees every handle that it took. So the handle table must hold
; only the shared context. A wrapper or a suite that did not free a handle
; shows here and nowhere else.
; ----------------------------------------------------------------------

(println)
(expect "only the shared context is outstanding once every suite has cleaned up"
        (create$ "context") (svn-handle-kinds))
(expect "and svn-client-context-free takes it" TRUE (svn-client-context-free ?*ctx*))
(expect "so nothing is outstanding" 0 (svn-handle-count))

(println)
(println "Tests run: " ?*tests-ran* "  Failures: " ?*tests-failed*)
(exit (if (> ?*tests-failed* 0) then 1 else 0))
