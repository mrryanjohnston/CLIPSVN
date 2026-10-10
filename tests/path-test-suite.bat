; ======================================================================
; Paths, URLs, times, and words: the functions of libsvn_subr that take no
; handle.
; ======================================================================

(println crlf "path-test-suite")

(defglobal ?*file-url* = (svn-file-url-from-dirent "tests/tmp"))

(deffunction path-test-suite ()
  (expect "svn-path-is-url knows a URL" TRUE (svn-path-is-url "file:///tmp/repos"))
  (expect "and a path" FALSE (svn-path-is-url "/tmp/repos"))
  (expect "and a symbol" TRUE (svn-path-is-url svn://example.com/repos))

  (expect "svn-uri-canonicalize removes a trailing slash" "file:///tmp/repos"
          (svn-uri-canonicalize "file:///tmp/repos/"))
  (expect "and encodes a space" "http://example.com/a%20b"
          (svn-uri-canonicalize "http://example.com/a b"))
  (expect "svn-dirent-canonicalize collapses slashes" "/tmp/a/b"
          (svn-dirent-canonicalize "/tmp//a/b/"))
  (expect "svn-relpath-canonicalize strips the leading slash" "a/b"
          (svn-relpath-canonicalize "/a//b/"))
  (expect "svn-dirent-is-absolute" TRUE (svn-dirent-is-absolute "/tmp"))
  (expect "and not for a relative path" FALSE (svn-dirent-is-absolute "tmp"))
  (expect-true "svn-dirent-get-absolute makes a relative path absolute"
               (svn-dirent-is-absolute (svn-dirent-get-absolute "tests/tmp")))
  (expect "svn-uri-is-canonical" TRUE (svn-uri-is-canonical "file:///tmp/repos"))
  (expect "and not with a trailing slash" FALSE (svn-uri-is-canonical "file:///tmp/repos/"))
  (expect "svn-dirent-is-canonical" TRUE (svn-dirent-is-canonical "/tmp/a"))
  (expect "and not with a double slash" FALSE (svn-dirent-is-canonical "/tmp//a"))

  (expect "svn-uri-basename" "c" (svn-uri-basename "file:///a/b/c"))
  (expect "svn-uri-dirname" "file:///a/b" (svn-uri-dirname "file:///a/b/c"))
  (expect "svn-dirent-basename" "c" (svn-dirent-basename "/a/b/c"))
  (expect "svn-dirent-dirname" "/a/b" (svn-dirent-dirname "/a/b/c/"))
  (expect "svn-uri-join" "file:///a/b/c%20d" (svn-uri-join "file:///a/b" "c d"))
  (expect "svn-dirent-join" "/a/b/c" (svn-dirent-join "/a/b" "c"))
  (expect "svn-uri-skip-ancestor" "c/d" (svn-uri-skip-ancestor "file:///a/b" "file:///a/b/c/d"))
  (expect "and nil when it is not an ancestor" nil (svn-uri-skip-ancestor "file:///a/b" "file:///x"))
  (expect "svn-dirent-skip-ancestor" "c" (svn-dirent-skip-ancestor "/a/b" "/a/b/c"))
  (expect "and the empty string for the same path" "" (svn-dirent-skip-ancestor "/a/b" "/a/b"))
  (expect "and nil otherwise" nil (svn-dirent-skip-ancestor "/a/b" "/a/c"))


  (expect-true "svn-file-url-from-dirent makes a file:// URL" (eq (str-index "file:///" ?*file-url*) 1))
  (expect "and svn-dirent-from-file-url takes it back"
          (svn-dirent-get-absolute "tests/tmp") (svn-dirent-from-file-url ?*file-url*))
  (bind ?r (svn-dirent-from-file-url "/not/a/url"))
  (expect "svn-dirent-from-file-url refuses a path" FALSE ?r)

  (expect "svn-time-to-cstring writes the epoch" "1970-01-01T00:00:00.000000Z" (svn-time-to-cstring 0))
  (expect "svn-time-from-cstring reads it back" 86400 (svn-time-from-cstring "1970-01-02T00:00:00.000000Z"))
  (bind ?r (svn-time-from-cstring "yesterday"))
  (expect "and refuses text that is not a date" FALSE ?r)

  (expect "svn-depth-to-word" "infinity" (svn-depth-to-word svn_depth_infinity))
  (expect "and takes the word too" "files" (svn-depth-to-word files))
  (expect "svn-depth-from-word" svn_depth_immediates (svn-depth-from-word "immediates"))
  (expect "and svn_depth_unknown for a word it does not know" svn_depth_unknown (svn-depth-from-word "sideways"))
  (expect "svn-node-kind-to-word" "dir" (svn-node-kind-to-word svn_node_dir))
  (expect "svn-node-kind-from-word" svn_node_file (svn-node-kind-from-word "file"))

  (expect "svn-opt-parse-revision reads a number" (create$ svn_opt_revision_number 12) (svn-opt-parse-revision "12"))
  (expect "and a keyword" (create$ svn_opt_revision_head nil) (svn-opt-parse-revision "HEAD"))
  (expect "and a range" (create$ svn_opt_revision_number 3 svn_opt_revision_number 7) (svn-opt-parse-revision "3:7"))
  (expect "and a date" svn_opt_revision_date (nth$ 1 (svn-opt-parse-revision "{2020-01-01}")))
  (bind ?r (svn-opt-parse-revision "yesterday"))
  (expect "and refuses other text" FALSE ?r))

(path-test-suite)
