; ======================================================================
; The library itself: versions, modules, handles, and errors.
; ======================================================================

(println crlf "version-test-suite")

(deffunction version-test-suite ()
  (expect-true "svn-version is a dotted version string"
               (> (str-length (svn-version)) 4))
  (expect-true "svn-version-number is at least 1.12"
               (>= (svn-version-number) 11200))
  (expect-true "svn-built-version is at least 1.12"
               (>= (svn-built-version) 11200))
  (expect "the loaded library is the one the binary was built against"
          (svn-built-version) (svn-version-number))
  (expect-true "svn-version-tag is nil or a string"
               (or (eq (svn-version-tag) nil) (stringp (svn-version-tag))))

  ; The access methods and filesystems that the build has. Every build has
  ; ra_local and fsfs: they are what the suites use.
  (expect-true "svn-ra-modules names ra_local" (str-index "ra_local" (svn-ra-modules)))
  (expect-true "and the file scheme" (str-index "file" (svn-ra-modules)))
  (expect-true "svn-fs-modules names fs_fs" (str-index "fs_fs" (svn-fs-modules)))

  (expect "the shared context is the one handle outstanding at the start" 1 (svn-handle-count))
  (expect "and svn-handle-kinds names it" (create$ "context") (svn-handle-kinds))

  ; The last error is remembered, and the ones before it are not.
  (expect "svn-error-last-code is an integer" TRUE (integerp (svn-error-last-code)))
  (bind ?r (svn-repos-open "tests/tmp/no-such-repository"))
  (expect "a repository that is not there is a refusal" FALSE ?r)
  (expect-true "svn-error-last says why" (stringp (svn-error-last)))
  (expect-true "svn-error-last-name is the symbolic name of the code" (symbolp (svn-error-last-name)))
  (expect-true "and svn-error-last-code is not zero" (<> 0 (svn-error-last-code))))

(version-test-suite)
