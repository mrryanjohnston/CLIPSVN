; ======================================================================
; Working copies: checkout, add, commit, update, status, info, list, cat,
; export, import, copy, move, delete, revert, switch, merge, locks.
; ======================================================================

(println crlf "wc-test-suite")

(defglobal ?*wc-repos* = (make-repos "wc"))

(defglobal ?*wc-url* = (repos-url "wc"))

(defglobal ?*wc* = "tests/tmp/wc-a")

(deffunction wc-test-suite ()
  (system "rm -rf tests/tmp/wc-a")
  (expect "svn-client-checkout of a new repository is revision 0" 0
          (svn-client-checkout ?*ctx* ?*wc-url* "tests/tmp/wc-a"))

  (expect-true "and the working copy is there" (read-line-of "tests/tmp/wc-a/.svn/format"))
  (expect "svn-client-get-wc-root is its root" (svn-dirent-get-absolute ?*wc*) (svn-client-get-wc-root ?*ctx* ?*wc*))
  (expect "svn-client-url-from-path is its URL" ?*wc-url* (svn-client-url-from-path ?*ctx* ?*wc*))
  (expect "svn-client-get-repos-root is the root and the uuid"
          (create$ ?*wc-url* (svn-repos-uuid ?*wc-repos*)) (svn-client-get-repos-root ?*ctx* ?*wc*))

  ; add and commit
  (write-file "tests/tmp/wc-a/a.txt" (str-cat "hello" ?*nl* "world" ?*nl*))
  (system "mkdir -p tests/tmp/wc-a/src")
  (write-file "tests/tmp/wc-a/src/main.c" "int main(void) { return 0; }")
  (write-file "tests/tmp/wc-a/scratch" "temporary")
  (expect "svn-client-add adds a file" TRUE (svn-client-add ?*ctx* "tests/tmp/wc-a/a.txt"))
  (expect "and a directory with what is in it" TRUE (svn-client-add ?*ctx* "tests/tmp/wc-a/src"))
  (bind ?r (svn-client-add ?*ctx* "tests/tmp/wc-a/a.txt"))
  (expect "a file that is added already is a refusal" FALSE ?r)
  (expect "unless forced" TRUE (svn-client-add ?*ctx* "tests/tmp/wc-a/a.txt" empty TRUE))
  (expect "svn-client-mkdir makes a directory in the working copy" TRUE
          (svn-client-mkdir ?*ctx* "tests/tmp/wc-a/docs"))
  (expect "and several at once, with parents" TRUE
          (svn-client-mkdir ?*ctx* (create$ "tests/tmp/wc-a/docs/a/b" "tests/tmp/wc-a/docs/c") TRUE))

  (bind ?st (svn-client-status ?*ctx* ?*wc*))
  (expect "svn-client-status lists everything that is not normal" 8 (svn-records-count ?st))
  (expect "as status records" status (svn-records-kind ?st))
  (expect "in path order, relative to the argument" "a.txt" (svn-record-field ?st 0 path))
  (expect "with the status" svn_wc_status_added (svn-record-field ?st 0 node-status))
  (expect "the kind" svn_node_file (svn-record-field ?st 0 kind))
  (expect "and the absolute path" (svn-dirent-get-absolute "tests/tmp/wc-a/a.txt") (svn-record-field ?st 0 abs-path))
  (expect "a file that is not versioned says so" svn_wc_status_unversioned (svn-record-field ?st 5 node-status))
  (expect "which is scratch" "scratch" (svn-record-field ?st 5 path))
  (expect "and a versioned one is versioned" TRUE (svn-record-field ?st 0 versioned))
  (expect "and the unversioned one is not" FALSE (svn-record-field ?st 5 versioned))
  (svn-records-free ?st)
  (bind ?st (svn-client-status ?*ctx* "tests/tmp/wc-a/a.txt"))
  (expect "the status of one file" 1 (svn-records-count ?st))
  (expect "names the file" "a.txt" (svn-record-field ?st 0 path))
  (svn-records-free ?st)
  (bind ?st (svn-client-status ?*ctx* ?*wc* nil infinity TRUE))
  (expect "get-all includes the paths that are normal" 9 (svn-records-count ?st))
  (expect "such as the root itself" "." (svn-record-field ?st 0 path))
  (svn-records-free ?st)

  (expect "svn-client-commit answers the new revision" 1
          (svn-client-commit ?*ctx* ?*wc* "the first commit"))
  (expect-true "and made notifications" (> (svn-client-notification-count ?*ctx*) 0))
  (bind ?n (svn-client-notifications ?*ctx*))
  (expect "the first is the addition of a.txt" svn_wc_notify_commit_added (svn-record-field ?n 0 action))
  (expect "for that path" (svn-dirent-get-absolute "tests/tmp/wc-a/a.txt") (svn-record-field ?n 0 path))
  (expect "the last is the end of the commit" svn_wc_notify_commit_finalizing
          (svn-record-field ?n (- (svn-records-count ?n) 1) action))
  (svn-records-free ?n)
  (expect "a commit with nothing to commit is nil" nil (svn-client-commit ?*ctx* ?*wc* "nothing"))
  (bind ?st (svn-client-status ?*ctx* ?*wc*))
  (expect "after the commit only scratch is left" 1 (svn-records-count ?st))
  (svn-records-free ?st)
  (expect "svn-client-min-max-revisions after a commit" (create$ 0 1) (svn-client-min-max-revisions ?*ctx* ?*wc*))
  (expect "svn-client-update brings the rest up" 1 (svn-client-update ?*ctx* ?*wc*))
  (expect "and now every path is at 1" (create$ 1 1) (svn-client-min-max-revisions ?*ctx* ?*wc*))
  (expect "svn-client-update with a multifield answers one" (create$ 1 1)
          (svn-client-update ?*ctx* (create$ "tests/tmp/wc-a/a.txt" "tests/tmp/wc-a/src")))

  ; info
  (bind ?info (svn-client-info ?*ctx* "tests/tmp/wc-a/a.txt"))
  (expect "svn-client-info of a file is one record" 1 (svn-records-count ?info))
  (expect "of kind info" info (svn-records-kind ?info))
  (expect "with the URL" (str-cat ?*wc-url* "/a.txt") (svn-record-field ?info 0 url))
  (expect "the revision" 1 (svn-record-field ?info 0 revision))
  (expect "the kind" svn_node_file (svn-record-field ?info 0 kind))
  (expect "the author of the last change" "tester" (svn-record-field ?info 0 last-changed-author))
  (expect "the schedule" svn_wc_schedule_normal (svn-record-field ?info 0 schedule))
  (expect "and the root of the working copy" (svn-dirent-get-absolute ?*wc*) (svn-record-field ?info 0 wc-root))
  (svn-records-free ?info)
  (bind ?info (svn-client-info ?*ctx* ?*wc* nil nil immediates))
  (expect "at depth immediates, the root and its children" 4 (svn-records-count ?info))
  (svn-records-free ?info)
  (bind ?info (svn-client-info ?*ctx* (str-cat ?*wc-url* "/src") HEAD))
  (expect "and a URL is asked of the repository" svn_node_dir (svn-record-field ?info 0 kind))
  (expect "which has no working copy part" nil (svn-record-field ?info 0 schedule))
  (svn-records-free ?info)

  ; list and cat
  (bind ?list (svn-client-list ?*ctx* ?*wc*))
  (expect "svn-client-list at depth immediates has the root and its children" 4 (svn-records-count ?list))
  (expect "as dirent records" dirent (svn-records-kind ?list))
  (expect "the root is ." "." (svn-record-field ?list 0 path))
  (expect "then the children in order" "a.txt" (svn-record-field ?list 1 path))
  (expect "with the size" 12 (svn-record-field ?list 1 size))
  (expect "and the author" "tester" (svn-record-field ?list 1 author))
  (svn-records-free ?list)
  (bind ?list (svn-client-list ?*ctx* ?*wc-url* nil nil infinity))
  (expect "and at depth infinity, everything" 8 (svn-records-count ?list))
  (svn-records-free ?list)
  (expect "svn-client-cat reads a file of the working copy" (str-cat "hello" ?*nl* "world" ?*nl*)
          (svn-client-cat ?*ctx* "tests/tmp/wc-a/a.txt"))
  (expect "and of the repository" (str-cat "hello" ?*nl* "world" ?*nl*)
          (svn-client-cat ?*ctx* (str-cat ?*wc-url* "/a.txt")))
  (bind ?r (svn-client-cat ?*ctx* (str-cat ?*wc-url* "/nothing.txt")))
  (expect "a file that is not there is a refusal" FALSE ?r)

  ; export and import
  (system "rm -rf tests/tmp/wc-export")
  (expect "svn-client-export exports the revision" 1 (svn-client-export ?*ctx* ?*wc-url* "tests/tmp/wc-export"))
  (expect "and the files are there" "hello" (read-line-of "tests/tmp/wc-export/a.txt"))
  (expect "and .svn is not" FALSE (read-line-of "tests/tmp/wc-export/.svn/format"))
  (bind ?r (svn-client-export ?*ctx* ?*wc-url* "tests/tmp/wc-export"))
  (expect "an export over an existing directory is a refusal" FALSE ?r)
  (expect "unless overwrite is TRUE" 1 (svn-client-export ?*ctx* ?*wc-url* "tests/tmp/wc-export" nil TRUE))
  (expect "svn-client-import commits a tree" 2
          (svn-client-import ?*ctx* "tests/tmp/wc-export" (str-cat ?*wc-url* "/imported") "import the export"))
  (expect "svn-client-update sees it" 2 (svn-client-update ?*ctx* ?*wc*))
  (expect "with the file" "hello" (read-line-of "tests/tmp/wc-a/imported/a.txt"))

  ; copy, move, delete
  (expect "svn-client-copy in the working copy" TRUE
          (svn-client-copy ?*ctx* "tests/tmp/wc-a/a.txt" "tests/tmp/wc-a/b.txt"))
  (bind ?st (svn-client-status ?*ctx* "tests/tmp/wc-a/b.txt"))
  (expect "makes an added copy" svn_wc_status_added (svn-record-field ?st 0 node-status))
  (expect "that says it is copied" TRUE (svn-record-field ?st 0 copied))
  (svn-records-free ?st)
  (expect "svn-client-move in the working copy" TRUE
          (svn-client-move ?*ctx* "tests/tmp/wc-a/src/main.c" "tests/tmp/wc-a/src/program.c"))
  (expect "svn-client-delete in the working copy" TRUE
          (svn-client-delete ?*ctx* "tests/tmp/wc-a/docs/c"))
  (bind ?st (svn-client-status ?*ctx* "tests/tmp/wc-a/src/main.c"))
  (expect "the moved file is deleted here" svn_wc_status_deleted (svn-record-field ?st 0 node-status))
  (expect "and knows where it went" (svn-dirent-get-absolute "tests/tmp/wc-a/src/program.c") (svn-record-field ?st 0 moved-to))
  (svn-records-free ?st)
  (expect "and the commit of all of that is 3" 3 (svn-client-commit ?*ctx* ?*wc* "copy, move, delete"))
  (expect "svn-client-copy of a URL commits at once" 4
          (svn-client-copy ?*ctx* (str-cat ?*wc-url* "/src") (str-cat ?*wc-url* "/branch") nil FALSE FALSE "branch src"))
  (expect "svn-client-mkdir of a URL too" 5
          (svn-client-mkdir ?*ctx* (str-cat ?*wc-url* "/tags") FALSE "make tags"))
  (expect "and svn-client-delete of a URL" 6
          (svn-client-delete ?*ctx* (str-cat ?*wc-url* "/tags") FALSE FALSE "remove tags"))
  (expect "and svn-client-move of a URL" 7
          (svn-client-move ?*ctx* (str-cat ?*wc-url* "/imported") (str-cat ?*wc-url* "/old") FALSE FALSE "rename imported"))
  (bind ?r (svn-client-copy ?*ctx* (str-cat ?*wc-url* "/nothing") (str-cat ?*wc-url* "/nowhere")))
  (expect "a copy of what is not there is a refusal" FALSE ?r)

  ; revert
  (write-file "tests/tmp/wc-a/a.txt" "changed")
  (bind ?st (svn-client-status ?*ctx* "tests/tmp/wc-a/a.txt"))
  (expect "a changed file is modified" svn_wc_status_modified (svn-record-field ?st 0 node-status))
  (expect "in its text" svn_wc_status_modified (svn-record-field ?st 0 text-status))
  (expect "not its properties" svn_wc_status_none (svn-record-field ?st 0 prop-status))
  (svn-records-free ?st)
  (expect "svn-client-revert reverts" TRUE (svn-client-revert ?*ctx* "tests/tmp/wc-a/a.txt"))
  (expect "and the content is back" "hello" (read-line-of "tests/tmp/wc-a/a.txt"))
  (write-file "tests/tmp/wc-a/a.txt" "changed")
  (write-file "tests/tmp/wc-a/b.txt" "changed")
  (expect "svn-client-revert with a depth takes a tree" TRUE (svn-client-revert ?*ctx* ?*wc* infinity))
  (bind ?st (svn-client-status ?*ctx* ?*wc*))
  (expect "so only scratch is left" 1 (svn-records-count ?st))
  (svn-records-free ?st)

  ; switch: a second working copy, switched to the branch
  (bind ?wc2 (make-wc "wc" "wc-b"))
  (expect "svn-client-switch switches to a URL" 7
          (svn-client-switch ?*ctx* (str-cat ?wc2 "/src") (str-cat ?*wc-url* "/branch")))
  (bind ?info (svn-client-info ?*ctx* (str-cat ?wc2 "/src")))
  (expect "and the path now has that URL" (str-cat ?*wc-url* "/branch") (svn-record-field ?info 0 url))
  (svn-records-free ?info)
  (bind ?st (svn-client-status ?*ctx* ?wc2 nil infinity TRUE))
  (expect "which status reports as switched" TRUE (svn-record-field ?st (record-index ?st path "src") switched))
  (svn-records-free ?st)

  ; merge: a change on the branch, merged to src
  (write-file (str-cat ?wc2 "/src/program.c") "int main(void) { return 1; }")
  (expect "a commit on the branch" 8 (svn-client-commit ?*ctx* ?wc2 "change the branch"))
  (svn-client-update ?*ctx* ?*wc*)
  (expect "svn-client-merge-peg dry run" TRUE
          (svn-client-merge-peg ?*ctx* (str-cat ?*wc-url* "/branch") "tests/tmp/wc-a/src" nil nil nil nil FALSE FALSE FALSE TRUE))
  (expect "which changed nothing" nil (svn-client-commit ?*ctx* ?*wc* "nothing"))
  (expect "svn-client-merge-peg of a range" TRUE
          (svn-client-merge-peg ?*ctx* (str-cat ?*wc-url* "/branch") "tests/tmp/wc-a/src" 7 8))
  (expect "and now the file has the change" "int main(void) { return 1; }" (read-line-of "tests/tmp/wc-a/src/program.c"))
  (expect "and the merge is committed" 9 (svn-client-commit ?*ctx* ?*wc* "merge the branch"))
  (svn-client-update ?*ctx* ?*wc*)
  (expect "svn-client-merge of two revisions" TRUE
          (svn-client-merge ?*ctx* (str-cat ?*wc-url* "/branch") 8 (str-cat ?*wc-url* "/branch") 7 "tests/tmp/wc-a/src"))
  (expect "reverses it" "int main(void) { return 0; }" (read-line-of "tests/tmp/wc-a/src/program.c"))
  (expect "svn-client-revert with a depth undoes the merge" TRUE (svn-client-revert ?*ctx* "tests/tmp/wc-a/src" infinity))

  ; locks, over file://
  (expect "svn-client-lock locks" TRUE (svn-client-lock ?*ctx* "tests/tmp/wc-a/a.txt" "mine"))
  (bind ?info (svn-client-info ?*ctx* "tests/tmp/wc-a/a.txt"))
  (expect "and info shows the owner" "tester" (svn-record-field ?info 0 lock-owner))
  (expect "and the comment" "mine" (svn-record-field ?info 0 lock-comment))
  (svn-records-free ?info)
  (bind ?list (svn-client-list ?*ctx* (str-cat ?*wc-url* "/a.txt") nil nil empty TRUE))
  (expect "and so does a listing that fetches locks" "tester" (svn-record-field ?list 0 lock-owner))
  (svn-records-free ?list)
  (expect "svn-client-unlock unlocks" TRUE (svn-client-unlock ?*ctx* "tests/tmp/wc-a/a.txt"))
  (bind ?info (svn-client-info ?*ctx* "tests/tmp/wc-a/a.txt"))
  (expect "and the owner is gone" nil (svn-record-field ?info 0 lock-owner))
  (svn-records-free ?info)

  ; housekeeping
  (expect "svn-client-cleanup cleans" TRUE (svn-client-cleanup ?*ctx* ?*wc*))
  (expect "svn-client-vacuum vacuums" TRUE (svn-client-vacuum ?*ctx* ?*wc*))
  (expect "and with remove-unversioned removes scratch" TRUE (svn-client-vacuum ?*ctx* ?*wc* TRUE))
  (expect "so scratch is gone" FALSE (read-line-of "tests/tmp/wc-a/scratch"))
  (expect "svn-client-upgrade has nothing to do" TRUE (svn-client-upgrade ?*ctx* ?*wc*))
  (system "rm -rf tests/tmp/wc-moved; mv tests/tmp/wc tests/tmp/wc-moved")
  (bind ?moved-url (svn-file-url-from-dirent "tests/tmp/wc-moved"))
  (bind ?r (svn-client-update ?*ctx* ?*wc*))
  (expect "a working copy whose repository moved cannot update" FALSE ?r)
  (expect "svn-client-relocate points it at the new place" TRUE (svn-client-relocate ?*ctx* ?*wc* ?*wc-url* ?moved-url))
  (expect "and it updates again" 9 (svn-client-update ?*ctx* ?*wc*))
  (system "mv tests/tmp/wc-moved tests/tmp/wc")
  (expect "svn-client-relocate back" TRUE (svn-client-relocate ?*ctx* ?*wc* ?moved-url ?*wc-url*))

  (bind ?r (svn-client-checkout ?*ctx* "file:///tests/tmp/no-such-repository" "tests/tmp/wc-none"))
  (expect "a checkout of what is not there is a refusal" FALSE ?r)
  (expect "with the message in svn-error-last" SVN_ERR_RA_CANNOT_CREATE_SESSION (svn-error-last-name))
  (svn-repos-free ?*wc-repos*))

(wc-test-suite)
