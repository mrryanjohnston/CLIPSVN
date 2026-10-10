; ======================================================================
; Contexts: configuration, authentication, and notifications.
; ======================================================================

(println crlf "context-test-suite")

(defglobal ?*ctx-repos* = (make-repos "context"))

(deffunction context-test-suite ()
  (bind ?ctx (svn-client-create-context))
  (expect-true "svn-client-create-context with no directory answers a handle" (handlep ?ctx))
  (expect "svn-client-context-free frees it" TRUE (svn-client-context-free ?ctx))

  (system "rm -rf tests/tmp/context-config")
  (bind ?ctx (svn-client-create-context "tests/tmp/context-config"))
  (expect-true "and with one" (handlep ?ctx))
  (expect-true "which it makes" (read-line-of "tests/tmp/context-config/config"))
  (expect "svn-client-get-config reads an option that is not set as nil" nil
          (svn-client-get-config ?ctx "miscellany" "global-ignores"))
  (expect "svn-client-set-config sets one" TRUE
          (svn-client-set-config ?ctx "miscellany" "global-ignores" "*.tmp"))
  (expect "and svn-client-get-config reads it" "*.tmp"
          (svn-client-get-config ?ctx "miscellany" "global-ignores"))
  (expect "svn-client-set-auth takes a name" TRUE (svn-client-set-auth ?ctx "someone"))
  (expect "and a password" TRUE (svn-client-set-auth ?ctx "someone" "secret"))
  (expect "and nil clears them" TRUE (svn-client-set-auth ?ctx nil))
  (expect "a new context has no notifications" 0 (svn-client-notification-count ?ctx))
  (bind ?n (svn-client-notifications ?ctx))
  (expect "and svn-client-notifications is an empty list" 0 (svn-records-count ?n))
  (expect "of notify records" notify (svn-records-kind ?n))
  (svn-records-free ?n)
  (svn-client-context-free ?ctx)

  ; The ignores in the configuration are what status ignores. The default
  ; ignores of Subversion, *.o among them, are not a good test of that.

  (bind ?wc (make-wc "context" "context-wc"))
  (write-file (str-cat ?wc "/thing.tmp") "temporary")
  (write-file (str-cat ?wc "/thing.c") "source")
  (bind ?st (svn-client-status ?*ctx* ?wc))
  (expect "the shared context ignores nothing" 2 (svn-records-count ?st))
  (svn-records-free ?st)
  (bind ?ctx (svn-client-create-context "tests/tmp/context-config"))
  (svn-client-set-config ?ctx "miscellany" "global-ignores" "*.tmp")
  (bind ?st (svn-client-status ?ctx ?wc))
  (expect "the configured context ignores *.tmp" 1 (svn-records-count ?st))
  (expect "and reports the other" "thing.c" (svn-record-field ?st 0 path))
  (svn-records-free ?st)
  (bind ?st (svn-client-status ?ctx ?wc nil infinity FALSE FALSE TRUE))
  (expect "unless no-ignore is TRUE" 2 (svn-records-count ?st))
  (svn-records-free ?st)
  (svn-client-context-free ?ctx)
  (svn-repos-free ?*ctx-repos*))

(context-test-suite)
