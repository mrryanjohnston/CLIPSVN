; ======================================================================
; What happens to a handle when the object that it points to is freed.
;
; This library records every handle that it gives out. A free of a context
; first frees every session that came from it. A handle that is freed, by its
; own free or by the free of its owner, is rejected everywhere.
; ======================================================================

(println crlf "handle-lifetime-test-suite")

(deffunction lifetime-suite ()
  (bind ?repos (make-repos "lifetime"))
  (bind ?url (repos-url "lifetime"))
  (bind ?start (svn-handle-count))

  ; A context and its sessions.
  (bind ?ctx (svn-client-create-context "tests/tmp/config"))
  (bind ?s1 (svn-client-open-ra-session ?ctx ?url))
  (bind ?s2 (svn-client-open-ra-session ?ctx ?url))
  (bind ?log (svn-ra-get-log ?s1 "" 0 0))
  (bind ?changes (svn-log-changed-paths ?log))
  (expect "every one of them is a handle" (+ ?start 5) (svn-handle-count))
  (bind ?kinds (svn-handle-kinds))
  (expect-true "svn-handle-kinds names a session after its context" (member$ "ra session (context)" ?kinds))
  (expect-true "and the changed paths after their log" (member$ "records (records)" ?kinds))
  (expect "svn-ra-session-free frees one session" TRUE (svn-ra-session-free ?s1))
  (bind ?r (svn-ra-get-latest-revnum ?s1))
  (expect "which is refused afterwards" FALSE ?r)
  (expect "while the other still works" 0 (svn-ra-get-latest-revnum ?s2))
  (expect "svn-client-context-free frees the context and its sessions" TRUE (svn-client-context-free ?ctx))
  (bind ?r (svn-ra-get-latest-revnum ?s2))
  (expect "so the other session is refused too" FALSE ?r)
  (bind ?r (svn-client-notification-count ?ctx))
  (expect "and the context" FALSE ?r)
  (bind ?r (svn-client-context-free ?ctx))
  (expect "and freeing the context again is refused" FALSE ?r)
  (expect "the log is not part of the context" 0 (svn-records-count ?log))
  (expect "svn-records-free frees it and the changed paths" TRUE (svn-records-free ?log))
  (bind ?r (svn-records-count ?changes))
  (expect "so the changed paths are refused" FALSE ?r)
  (expect "with the table back where it was" ?start (svn-handle-count))

  ; Handles have kinds. A repository in the place of a context is rejected
  ; by name.
  (bind ?r (svn-client-notification-count ?repos))
  (expect "a repository is refused where a context belongs" FALSE ?r)
  (bind ?r (svn-repos-youngest-rev ?*ctx*))
  (expect "and a context where a repository belongs" FALSE ?r)
  (bind ?r (svn-records-count ?repos))
  (expect "and a repository where records belong" FALSE ?r)
  (bind ?r (svn-ra-get-uuid ?repos))
  (expect "and a repository where a session belongs" FALSE ?r)

  ; A repository handle is independent of every context.
  (expect "svn-repos-free frees the repository" TRUE (svn-repos-free ?repos))
  (bind ?r (svn-repos-youngest-rev ?repos))
  (expect "after which it is refused" FALSE ?r)
  (bind ?r (svn-repos-free ?repos))
  (expect "and so is a second free" FALSE ?r)
  (expect "and the table is back where it was" (- ?start 1) (svn-handle-count)))

(lifetime-suite)
