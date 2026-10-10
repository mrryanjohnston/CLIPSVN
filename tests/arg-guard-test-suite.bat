; ======================================================================
; What the wrappers do with arguments that CLIPS did not check.
;
; CLIPS checks a literal argument against the AddUDF restriction string when
; it reads the call. So a literal of the wrong type does not reach the
; wrapper. CLIPS does not check a value that comes through a variable. Every
; wrapper checks it again, and this suite tests that check. This is why the
; suite uses globals. They are the only way to get a bad value past the parser
; and into the guard. It also runs at the top level, not in a deffunction: the
; evaluation error that a rejected argument causes would halt a deffunction.
;
; Each of these calls writes a line to stderr and then returns FALSE. That
; output is the test, not noise.
; ======================================================================

(println crlf "arg-guard-test-suite")

(defglobal
  ?*not-a-handle*  = 42
  ?*a-symbol*      = some-symbol
  ?*a-string*      = "some string"
  ?*a-float*       = 1.5
  ?*a-multifield*  = (create$ 1 2 3)
  ?*negative*      = -1
  ?*guard-result*  = FALSE)

(defglobal ?*guard-repos* = (make-repos "guard"))

(defglobal ?*guard-url* = (repos-url "guard"))

(defglobal ?*guard-wc* = (make-wc "guard" "guard-wc"))

(defglobal ?*guard-records* = (svn-client-notifications ?*ctx*))

; ----------------------------------------------------------------------
; A value that is not a handle
; ----------------------------------------------------------------------

(bind ?*guard-result* (svn-client-context-free ?*not-a-handle*))
(expect "svn-client-context-free refuses an integer" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-status ?*a-string* ?*guard-wc*))
(expect "svn-client-status refuses a string context" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-records-count ?*a-symbol*))
(expect "svn-records-count refuses a symbol" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-repos-youngest-rev ?*a-multifield*))
(expect "svn-repos-youngest-rev refuses a multifield" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-ra-get-uuid ?*a-float*))
(expect "svn-ra-get-uuid refuses a float" FALSE ?*guard-result*)

; ----------------------------------------------------------------------
; Arguments of the correct type with a wrong value
; ----------------------------------------------------------------------

(bind ?*guard-result* (svn-client-checkout ?*ctx* ?*guard-wc* "tests/tmp/guard-wc2"))
(expect "svn-client-checkout refuses a path where a URL belongs" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-checkout ?*ctx* ?*guard-url* ?*guard-url*))
(expect "and a URL where a path belongs" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-checkout ?*ctx* ?*guard-url* "tests/tmp/guard-wc2" ?*negative*))
(expect "and a negative revision" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-checkout ?*ctx* ?*guard-url* "tests/tmp/guard-wc2" ?*a-float*))
(expect "and a float for a revision" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-checkout ?*ctx* ?*guard-url* "tests/tmp/guard-wc2" "yesterday"))
(expect "and text that is not a revision" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-checkout ?*ctx* ?*guard-url* "tests/tmp/guard-wc2" "1:2"))
(expect "and a range where one revision belongs" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-checkout ?*ctx* ?*guard-url* "tests/tmp/guard-wc2" HEAD sideways))
(expect "and a depth it does not know" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-checkout ?*ctx* ?*guard-url* "tests/tmp/guard-wc2" HEAD infinity ?*a-symbol*))
(expect "and a symbol where TRUE or FALSE belongs" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-update ?*ctx* (create$)))
(expect "svn-client-update refuses an empty multifield of paths" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-update ?*ctx* ?*a-multifield*))
(expect "and a multifield of integers" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-resolve ?*ctx* ?*guard-wc* somehow))
(expect "svn-client-resolve refuses a choice it does not know" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-merge-peg ?*ctx* ?*guard-url* ?*guard-wc* 1))
(expect "svn-client-merge-peg refuses a range with no end" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-client-diff ?*ctx* ?*guard-url* nil ?*guard-url* nil))
(expect "svn-client-diff refuses unspecified revisions" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-ra-rev-prop ?*ctx* 1 "svn:log"))
(expect "svn-ra-rev-prop refuses a context where a session belongs" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-repos-check-path ?*guard-repos* "/" ?*negative*))
(expect "svn-repos-check-path refuses a negative revision" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-repos-check-path ?*guard-repos* "/" 99))
(expect "and a revision that is not there" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-repos-revprop-get ?*guard-repos* ?*a-string* "svn:log"))
(expect "svn-repos-revprop-get refuses a string revision" FALSE ?*guard-result*)



(bind ?*guard-result* (svn-record-byindex ?*guard-records* ?*negative*))
(expect "svn-record-byindex refuses a negative index" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-record-to-fact ?*guard-records* 0 ?*a-string*))
(expect "svn-record-to-fact refuses a string for the template name" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-records-to-facts ?*guard-records* ?*a-symbol*))
(expect "svn-records-to-facts refuses a deftemplate that is not there" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-time-to-cstring ?*a-string*))
(expect "svn-time-to-cstring refuses a string" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-depth-to-word ?*a-float*))
(expect "svn-depth-to-word refuses a float" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-repos-create ?*guard-url*))
(expect "svn-repos-create refuses a URL" FALSE ?*guard-result*)

(bind ?*guard-result* (svn-repos-create "tests/tmp/guard-other" "bdb"))
(expect "and a filesystem type the build does not have" FALSE ?*guard-result*)

(svn-records-free ?*guard-records*)
(svn-repos-free ?*guard-repos*)
