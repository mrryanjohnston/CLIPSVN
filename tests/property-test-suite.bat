; ======================================================================
; Properties of paths and of revisions.
; ======================================================================

(println crlf "property-test-suite")

(defglobal ?*prop-repos* = (make-repos "props"))

(defglobal ?*prop-url* = (repos-url "props"))

(defglobal ?*prop-wc* = (make-wc "props" "props-wc"))

(deffunction property-test-suite ()
  (commit-file ?*prop-wc* "a.txt" "a" "add a")

  (expect "a property that is not set is nil" nil (svn-client-propget ?*ctx* "svn:mime-type" (str-cat ?*prop-wc* "/a.txt")))
  (expect "svn-client-propset-local sets one" TRUE
          (svn-client-propset-local ?*ctx* "svn:mime-type" "text/plain" (str-cat ?*prop-wc* "/a.txt")))
  (expect "and svn-client-propget reads it" "text/plain"
          (svn-client-propget ?*ctx* "svn:mime-type" (str-cat ?*prop-wc* "/a.txt")))
  (bind ?st (svn-client-status ?*ctx* (str-cat ?*prop-wc* "/a.txt")))
  (expect "status shows the property change" svn_wc_status_modified (svn-record-field ?st 0 prop-status))
  (expect "and not a text change" svn_wc_status_normal (svn-record-field ?st 0 text-status))
  (svn-records-free ?st)
  (expect "svn-client-propset-local sets a custom one" TRUE
          (svn-client-propset-local ?*ctx* "custom:owner" "ada" (str-cat ?*prop-wc* "/a.txt")))
  (bind ?props (svn-client-proplist ?*ctx* (str-cat ?*prop-wc* "/a.txt")))
  (expect "svn-client-proplist has both" 2 (svn-records-count ?props))
  (expect "of kind prop" prop (svn-records-kind ?props))
  (expect "in name order" "custom:owner" (svn-record-field ?props 0 name))
  (expect "with the value" "ada" (svn-record-field ?props 0 value))
  (expect "and the path" (svn-dirent-get-absolute (str-cat ?*prop-wc* "/a.txt")) (svn-record-field ?props 0 path))
  (svn-records-free ?props)
  (expect "the commit of a property change" 2 (svn-client-commit ?*ctx* ?*prop-wc* "set properties"))
  (expect "and it is in the repository" "ada" (svn-client-propget ?*ctx* "custom:owner" (str-cat ?*prop-url* "/a.txt")))
  (expect "svn-client-propset-local with nil deletes" TRUE
          (svn-client-propset-local ?*ctx* "custom:owner" nil (str-cat ?*prop-wc* "/a.txt")))
  (expect "so it is gone from the working copy" nil (svn-client-propget ?*ctx* "custom:owner" (str-cat ?*prop-wc* "/a.txt")))
  (expect "but not from the repository" "ada" (svn-client-propget ?*ctx* "custom:owner" (str-cat ?*prop-url* "/a.txt") HEAD))
  (svn-client-revert ?*ctx* (str-cat ?*prop-wc* "/a.txt"))
  (bind ?r (svn-client-propset-local ?*ctx* "svn:eol-style" "sideways" (str-cat ?*prop-wc* "/a.txt")))
  (expect "a value Subversion does not accept is a refusal" FALSE ?r)
  (expect "unless the checks are skipped" TRUE
          (svn-client-propset-local ?*ctx* "svn:eol-style" "sideways" (str-cat ?*prop-wc* "/a.txt") empty TRUE))
  (svn-client-revert ?*ctx* (str-cat ?*prop-wc* "/a.txt"))
  (expect "svn-client-propset-remote commits a property" 3
          (svn-client-propset-remote ?*ctx* "custom:remote" "yes" (str-cat ?*prop-url* "/a.txt") 2 "set remotely"))
  (expect "and the repository has it" "yes" (svn-client-propget ?*ctx* "custom:remote" (str-cat ?*prop-url* "/a.txt")))
  (bind ?props (svn-client-proplist ?*ctx* ?*prop-url* HEAD HEAD infinity))
  (expect "svn-client-proplist at depth infinity has everything" 3 (svn-records-count ?props))
  (svn-records-free ?props)
  (bind ?props (svn-repos-node-proplist ?*prop-repos* "a.txt"))
  (expect "and so does the repository" 3 (svn-records-count ?props))
  (svn-records-free ?props)

  ; revision properties through the client need the hook
  (expect "svn-client-revprop-get reads the log message" "add a"
          (svn-client-revprop-get ?*ctx* "svn:log" ?*prop-url* 1))
  (bind ?r (svn-client-revprop-set ?*ctx* "svn:log" "changed" ?*prop-url* 1))
  (expect "a change without the hook is a refusal" FALSE ?r)
  (allow-revprop-changes ?*prop-repos*)
  (expect "and with it, the revision" 1 (svn-client-revprop-set ?*ctx* "svn:log" "changed" ?*prop-url* 1))
  (expect "which now reads back" "changed" (svn-client-revprop-get ?*ctx* "svn:log" ?*prop-url* 1))
  (bind ?props (svn-client-revprop-list ?*ctx* ?*prop-url* 1))
  (expect "svn-client-revprop-list has the three" 3 (svn-records-count ?props))
  (expect "of kind revprop" revprop (svn-records-kind ?props))
  (expect "svn:author first" "svn:author" (svn-record-field ?props 0 name))
  (expect "which is tester" "tester" (svn-record-field ?props 0 value))
  (svn-records-free ?props)
  (expect "svn-client-revprop-set with nil deletes" 1
          (svn-client-revprop-set ?*ctx* "svn:author" nil ?*prop-url* 1 TRUE))
  (expect "so the author is gone" nil (svn-client-revprop-get ?*ctx* "svn:author" ?*prop-url* 1))

  (svn-repos-free ?*prop-repos*))

(property-test-suite)
