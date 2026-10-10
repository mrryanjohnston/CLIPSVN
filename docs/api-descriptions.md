# Descriptions

One `### <function>` block per registered function. `docs/gen-api-docs.sh`
folds these into API.md and fails if any function is missing one, or if any
block here names a function that is not registered.

### svn-version
The version of the Subversion libraries loaded right now, as a string:
`"1.14.5"`.

### svn-version-number
The same version as a number: major times ten thousand, minor times a
hundred, plus the patch. 1.14.5 is `11405`.

### svn-built-version
Not a Subversion function. The version of the Subversion the wrappers were
*compiled* against, in the same numbers. With the vendored build it is also
what is loaded, so the two agree; with `SVN_SYSTEM=1` they can differ.

### svn-version-tag
The tag of the loaded version -- `" (dev build)"`, `" (r1234)"` -- or
`nil` for a release.

### svn-ra-modules
What the loaded Subversion can reach a repository with, as the text that
`svn --version` prints: one line per module, `ra_local` for `file://`,
`ra_svn` for `svn://`, and `ra_serf` for `http://` and `https://` when the
build has it. The vendored build has the first two.

### svn-fs-modules
The repository filesystems the loaded Subversion has, in the same form:
`fs_fs` and `fs_x`.

### svn-handle-count
Not a Subversion function. How many handles this library has handed out
and not yet had back. A program that frees what it takes ends with zero.

### svn-handle-kinds
Not a Subversion function. The kind of every open handle, as a multifield
of strings: `"context"`, `"ra session"`, `"repository"`, `"records"`. A
handle that belongs to another names it in parentheses, as
`"ra session (context)"` or `"records (records)"`. For finding what a
program forgot to free.

### svn-error-last
The message of the last error Subversion reported, or `nil` when there
has been none. An error is a chain of messages from the outside in, joined
with semicolons. Every refusal in this library writes the same message to
stderr as it happens, so this is for a program that wants to keep it. It
is not cleared by a call that succeeds: the refusal is what says there was
an error, and this is what it was.

### svn-error-last-code
The APR status code of that error, as an integer: `160006` for a revision
that does not exist. Zero when there has been none.

### svn-error-last-name
The symbolic name of that code, as Subversion spells it:
`SVN_ERR_FS_NO_SUCH_REVISION`. `nil` when there has been none.

### svn-path-is-url
Whether the text is a URL: it has a scheme and `://`. `file:///tmp/r`
is one; `/tmp/r` is not.

### svn-uri-canonicalize
A URL in the one form Subversion uses: no trailing slash, the scheme and
host in lower case, a space as `%20`. Every URL that this library takes is
put through this first.

### svn-dirent-canonicalize
A local path in the one form Subversion uses: no trailing slash, no double
slash. Every path that this library takes is put through this, and made
absolute, first.

### svn-relpath-canonicalize
A path relative to something, in the one form Subversion uses: no leading
or trailing slash.

### svn-dirent-get-absolute
A path made absolute against the current directory of the process, and
canonical.

### svn-dirent-is-absolute
Whether a path is absolute.

### svn-uri-is-canonical
Whether a URL is already what `svn-uri-canonicalize` would make of it.

### svn-dirent-is-canonical
The same for a path.

### svn-uri-basename
The last component of a URL, decoded: `"c"` for `file:///a/b/c`.

### svn-uri-dirname
A URL with its last component removed.

### svn-dirent-basename
The last component of a path.

### svn-dirent-dirname
A path with its last component removed.

### svn-uri-join
A URL with a component added, encoded as it needs to be.

### svn-dirent-join
A path with a component added.

### svn-uri-skip-ancestor
The part of a URL below an ancestor URL, as a relative path: `"c/d"` for
`file:///a/b` and `file:///a/b/c/d`; `""` for the same URL twice; `nil`
when the first is not an ancestor of the second.

### svn-dirent-skip-ancestor
The same for two paths.

### svn-file-url-from-dirent
The `file://` URL of a local path, which is how a repository on this
machine is named to the client functions. The path is made absolute
first. `svn-repos-file-url` answers the same for an open repository.

### svn-dirent-from-file-url
The local path of a `file://` URL. A refusal for a URL of another scheme.

### svn-time-to-cstring
A time in seconds since the epoch, as the string Subversion writes
everywhere: `"2026-09-05T12:34:56.000000Z"`.

### svn-time-from-cstring
The reverse: seconds since the epoch for a string in that form, or in any
other form Subversion reads. A refusal for text that is not a date.

### svn-depth-to-word
The word the command line uses for a depth: `"infinity"` for
`svn_depth_infinity`. The word itself is accepted too.

### svn-depth-from-word
The depth for a word, as an `svn_depth_*` symbol; `svn_depth_unknown` for
a word that is not one.

### svn-node-kind-to-word
The word for a node kind: `"dir"` for `svn_node_dir`.

### svn-node-kind-from-word
The node kind for a word, as an `svn_node_*` symbol.

### svn-opt-parse-revision
A revision or a revision range, parsed as the command line parses `-r`:
`"12"`, `"HEAD"`, `"{2026-01-01}"`, `"3:7"`. Answers a multifield of a
kind and a value for each revision: `(svn_opt_revision_number 12)`,
`(svn_opt_revision_head nil)`, and four elements for a range. A date
crosses as seconds since the epoch. A refusal for text that is neither.

### svn-client-create-context
A context: what every client function takes first. It holds the
configuration, the authentication, and the notifications of the last
operation. The optional argument is a configuration directory in place of
`~/.subversion`; it is made if it is not there, with the usual template
files, and a program that wants no part of this machine's configuration
gives one. Freed with `svn-client-context-free`.

The context is non-interactive: it cannot ask for a password. It answers a
challenge with what `svn-client-set-auth` gave, with the credentials cached
in the configuration directory, and with the certificate files the
configuration names, and it caches nothing new.

### svn-client-context-free
Gives a context back, and every session that came from it first. Each of
those handles is refused afterwards, and so is this one. The lists of
records that the context made are not affected: they belong to the
program.

### svn-client-set-auth
The username, and optionally the password, that the context answers a
challenge with. Over `file://` there is no challenge, and the username is
what a commit is recorded under. `nil` clears either. The authentication
remembers its first answer for a repository, so this replaces it: a
session that is already open keeps the old one.

### svn-client-set-config
Sets one option of the `config` file of the context, in memory: the section
`"miscellany"`, the option `"global-ignores"`, the value. The file is not
written. `nil` for the value sets it to the empty string.

### svn-client-get-config
The value of one option of that file, or `nil` when it is not set.

### svn-client-notifications
Not a Subversion function. Every notification the last operation of the
context made, as a list of `notify` records: one for each path a commit
sent, each path an update changed, each path a status walk finished. The
fields are `action` (an `svn_wc_notify_*` symbol), `path`, `kind`,
`revision`, `content-state`, `prop-state`, `lock-state`, `url`,
`mime-type`, `changelist`, `error`, `old-revision` and `prop-name`. The
list is a copy: the context keeps its own until the next operation, and
the program frees this one with `svn-records-free`.

### svn-client-notification-count
How many notifications the last operation made, without the list.

### svn-client-checkout
Checks a URL out into a path, and answers the revision it checked out. The
revision defaults to `HEAD`; the depth to `infinity`; `TRUE` for the last
argument leaves externals out. The path is made if it is not there.

### svn-client-update
Updates a path, or a multifield of paths, to a revision, `HEAD` unless
another is given, and answers the revision each is now at: an integer for
one path, a multifield for a multifield. The depth defaults to the depth
the working copy has; `TRUE` for depth-is-sticky makes the given depth the
working copy's; `TRUE` for the last argument leaves externals out.

### svn-client-switch
Switches a working copy path to another URL of the same repository -- a
branch, a tag -- and answers the revision. The revision defaults to
`HEAD`, the depth to `infinity`.

### svn-client-add
Schedules a path for addition. A directory is added with what is in it, to
the depth given, `infinity` unless another is. `TRUE` for force adds a path
that is added already without a refusal; `TRUE` for no-ignore adds what the
ignore patterns would skip; `TRUE` for add-parents adds the unversioned
directories above the path too.

### svn-client-mkdir
Makes a directory, or each of a multifield of them. A path is scheduled in
the working copy and answers `TRUE`; a URL is committed at once with the
message, and answers the new revision. `TRUE` for make-parents makes the
directories above it as well.

### svn-client-delete
Deletes a path or each of a multifield of them: scheduled in the working
copy, or committed at once for a URL, with the same answers as
`svn-client-mkdir`. `TRUE` for force deletes a path with local changes;
`TRUE` for keep-local schedules the deletion but leaves the file on disk.

### svn-client-copy
Copies a source, at a revision, to a destination. Working copy to working
copy schedules the copy; anything to a URL commits it with the message and
answers the revision. The revision defaults to the working version of a
path and `HEAD` of a URL. `TRUE` for copy-as-child puts the source inside a
destination that exists; `TRUE` for make-parents makes the directories
above the destination.

### svn-client-move
Moves a source to a destination, with the same two forms and answers as
`svn-client-copy`.

### svn-client-commit
Commits a path or a multifield of them with the message, to the depth
given, and answers the new revision. `nil` when there was nothing to
commit. `TRUE` for keep-locks keeps the locks the commit would release.

### svn-client-revert
Reverts a path or a multifield of them to the depth given, `empty` unless
another is: `infinity` reverts a tree.

### svn-client-resolve
Marks a conflict resolved, with one of the choices the command line's
`--accept` takes: `svn_wc_conflict_choose_mine_full`, or the word
`mine-full`; `theirs-full`; `base`; `working` for what is in the working
copy now; `mine-conflict` and `theirs-conflict` for the conflicted regions
only. The depth defaults to `empty`.

### svn-client-cleanup
Cleans a working copy up: breaks the write locks it holds (`TRUE` unless
`FALSE` is given), fixes the recorded timestamps (`TRUE` unless `FALSE`),
removes the pristine copies nothing refers to, and does the same for
externals.

### svn-client-vacuum
Removes what a working copy does not need: unversioned items, ignored
items, the pristine copies nothing refers to. Each `FALSE` unless `TRUE`
is given, in that order, with fix-timestamps between the second and the
third and include-externals last.

### svn-client-relocate
Points a working copy at a repository that moved: every URL in it that
starts with the from prefix has that replaced by the to prefix. The
repository must be reachable at the new place, so its identity can be
checked.

### svn-client-upgrade
Upgrades a working copy made by an older Subversion to the format this one
uses. Nothing to do for a current one.

### svn-client-lock
Locks a path or a multifield of them in the repository, with an optional
comment. `TRUE` for steal takes a lock someone else holds.

### svn-client-unlock
Unlocks them. `TRUE` for break removes a lock someone else holds.

### svn-client-export
Copies a tree out of a repository or a working copy into a directory,
without the `.svn` administrative area, and answers the revision. The
revision defaults to the working version of a path and `HEAD` of a URL.
`TRUE` for overwrite writes into a directory that exists.

### svn-client-import
Commits an unversioned tree into a URL with the message, and answers the
new revision. The directory itself is not imported: what is in it goes
under the URL. `TRUE` for no-ignore imports what the ignore patterns would
skip.

### svn-client-cat
The content of a file, as a string: from the working copy for a path,
from the repository for a URL, at the revision given or that default.
The peg revision, when given, is the revision the target is looked up in.
Keywords are expanded unless `FALSE` is given last. A string cannot hold a
NUL byte, so a binary file is read up to its first one.

### svn-client-get-wc-root
The root of the working copy a path is in.

### svn-client-get-repos-root
The root URL and the uuid of the repository behind a path or a URL, as a
two-element multifield.

### svn-client-url-from-path
The URL a working copy path corresponds to, or a URL as it is.

### svn-client-min-max-revisions
The lowest and the highest revision in a working copy, as a two-element
multifield. A working copy where they differ is mixed-revision, which an
update settles. `TRUE` for committed asks for the last-changed revisions
instead.

### svn-client-status
The status of a working copy path and what is under it, as a list of
`status` records: one for each path that is not simply up to date, or for
every path when get-all is `TRUE`. The fields are `path` (relative to the
argument, `.` for the argument itself), `node-status`, `text-status` and
`prop-status` (`svn_wc_status_*` symbols), `kind`, `revision`,
`changed-rev`, `changed-date`, `changed-time`, `changed-author`,
`versioned`, `conflicted`, `copied`, `switched`, `locked`,
`file-external`, `changelist`, `depth`, `repos-relpath`,
`repos-root-url`, `repos-node-status`, `repos-text-status`,
`repos-prop-status`, `ood-changed-rev`, `ood-changed-author`,
`lock-owner`, `repos-lock-owner`, `moved-from`, `moved-to`, and
`abs-path`. The `repos-*` and `ood-*` fields say what the repository has
when check-out-of-date is `TRUE`; the revision is the one to compare
against, `HEAD` unless another is given. `TRUE` for no-ignore lists the
ignored paths too; `TRUE` for ignore-externals leaves externals out.

### svn-client-log
The history of a target, or of a multifield of them, as a list of `log`
records, one for each revision: `revision`, `author`, `date`, `time`,
`message`, `has-children` and `changed-path-count`. The changed paths of
every revision are a second list that `svn-log-changed-paths` answers.

The range is start to end, in that order: `HEAD` to `1` for the newest
first. A range that is not given is `HEAD` to `0` for a URL and `BASE` to
`0` for a working copy path; a start alone is that one revision. Revision
0, the repository before its first commit, is left out as the command line
leaves it out, unless a message was put on it. A limit above zero stops
after that many; `FALSE` for discover-changed-paths leaves the changed
paths uncollected, which is faster; `TRUE` for strict-node-history stops
at a copy instead of following it. The peg revision is the revision the
target is looked up in.

### svn-log-changed-paths
Not a Subversion function. The paths that the revisions of a log changed,
as a list of `changed-path` records: `revision`, `action` (the symbols
`A`, `D`, `M` and `R`), `path`, `kind`, `copyfrom-path`, `copyfrom-rev`,
`text-modified` and `props-modified` (`svn_tristate_*` symbols). The list
belongs to the log: it is refused by `svn-records-free` and goes when the
log does.

### svn-client-info
Information about a path or a URL, and what is under it to the depth
given, `empty` unless another is, as a list of `info` records: `path`,
`url`, `revision`, `repos-root-url`, `repos-uuid`, `kind`, `size`,
`last-changed-rev`, `last-changed-date`, `last-changed-time`,
`last-changed-author`, `lock-owner`, `lock-token`, `lock-comment`, and
for a working copy path `schedule` (an `svn_wc_schedule_*` symbol),
`copyfrom-url`, `copyfrom-rev`, `depth`, `wc-root`, `changelist`,
`checksum`, `moved-from` and `moved-to`. With no revision, a working copy
path is answered from the working copy alone, without a repository access.

### svn-client-list
A directory listing of a path or a URL, as a list of `dirent` records:
`path` (relative to the target, `.` for the target itself), `kind`,
`size`, `has-props`, `created-rev`, `date`, `time`, `author`,
`lock-owner`, `lock-token`, `lock-comment` and `abs-path`. The listing is
of the repository: at `HEAD` for a URL, at the base revision for a working
copy path, unless a revision is given. The depth defaults to `immediates`.
The locks are fetched only when the last argument is `TRUE`.

### svn-client-blame
Who changed each line of a file, as a list of `blame` records, one per
line: `line-number` (from 1), `revision`, `author`, `date`, `time`,
`merged-revision`, `merged-path`, `merged-author`, `line` and
`local-change`. The start revision defaults to 1 and the end to the base
revision of a path or `HEAD` of a URL. A refusal for a binary file.

### svn-client-diff
The difference between two targets at two revisions, as the text of a
unified diff. Both revisions are required: `BASE` and `WORKING` for the
local changes of a working copy. The depth defaults to `infinity`; `TRUE`
for ignore-ancestry compares by path alone; `TRUE` for git-format writes
the header git writes.

### svn-client-diff-summarize
The same difference as a list of `diff-summary` records, one for each path
that differs: `path`, `summarize-kind` (an
`svn_client_diff_summarize_kind_*` symbol), `prop-changed` and
`node-kind`.

### svn-client-merge
Merges the difference between two sources at two revisions into a working
copy path: `svn merge source1@rev1 source2@rev2 target`. The depth
defaults to the working copy's; `TRUE` for ignore-ancestry compares by
path alone; `TRUE` for force-delete deletes files with local changes;
`TRUE` for record-only records the merge without making the changes;
`TRUE` for dry-run makes no changes at all. A mixed-revision working copy
is refused, as the command line refuses it: update first.

### svn-client-merge-peg
Merges from one source into a working copy path: the revisions from start
to end when both are given, or everything eligible when neither is, which
is the automatic merge of `svn merge source target`. The peg revision is
the revision the source is looked up in. The other arguments are those of
`svn-client-merge`.

### svn-client-propget
The value of one property of a path or a URL, or `nil` when it is not
set. From the working copy for a path and `HEAD` for a URL, unless a
revision is given; the peg revision is the revision the target is looked
up in.

### svn-client-proplist
Every property of a path or a URL, and of what is under it to the depth
given, `empty` unless another is, as a list of `prop` records: `path`,
`name` and `value`, in name order.

### svn-client-propset-local
Sets a property on a path or a multifield of them, in the working copy,
to the depth given. `nil` for the value deletes the property. A value
Subversion does not accept for an `svn:` property is a refusal, unless
skip-checks is `TRUE`.

### svn-client-propset-remote
Sets a property on a URL, committed at once with the message, and answers
the new revision. The base revision is the revision the URL is expected to
be at, and the commit is refused if it has changed since. `nil` for the
value deletes the property.

### svn-client-revprop-get
The value of a revision property -- `svn:log`, `svn:author`, `svn:date`,
or a custom one -- of a revision of the repository at a URL, or `nil`.

### svn-client-revprop-set
Sets a revision property, and answers the revision. `nil` for the value
deletes it; `TRUE` for force sets `svn:log` and its relatives without
checking the value. A repository refuses a change to a revision property
unless it has a `pre-revprop-change` hook that allows it: a script by that
name in `svn-repos-hook-dir`. `svn-repos-revprop-set` bypasses the hook.

### svn-client-revprop-list
Every revision property of a revision, as a list of `revprop` records:
`name` and `value`, in name order.

### svn-client-open-ra-session
A session with the repository at a URL, for the `svn-ra-*` functions: the
repository read directly, without a working copy. The session uses the
authentication and configuration of the context, belongs to it, and is
freed with `svn-ra-session-free` or with the context.

### svn-ra-session-free
Gives a session back.

### svn-ra-get-latest-revnum
The youngest revision of the repository.

### svn-ra-get-uuid
The uuid of the repository.

### svn-ra-get-repos-root
The root URL of the repository, which the session URL is at or under.

### svn-ra-get-session-url
The URL the session is on: the one it was opened on, or the one
`svn-ra-reparent` moved it to.

### svn-ra-reparent
Moves the session to another URL of the same repository. Every path
argument is relative to the session URL, so this is how a session is
pointed at a subdirectory.

### svn-ra-check-path
What is at a path relative to the session URL -- `""` or `nil` for the
session URL itself -- in a revision, the youngest unless one is given, as
an `svn_node_*` symbol: `svn_node_none` for nothing.

### svn-ra-stat
A `dirent` record for a path in a revision, in a list of one, or of none
when there is nothing at the path. The fields are those of
`svn-client-list`.

### svn-ra-get-file
The content of a file at a path in a revision, as a string.

### svn-ra-get-file-props
The properties of a file at a path in a revision, as `prop` records. The
entry properties Subversion adds -- `svn:entry:committed-rev` and its
relatives -- are among them.

### svn-ra-get-dir
The entries of a directory at a path in a revision, as `dirent` records
in name order. The `path` of each is relative to the session URL.

### svn-ra-get-log
The history of a path, or of a multifield of them, relative to the session
URL -- `""` for the session URL itself -- as `log` records, with the same
fields and the same second list as `svn-client-log`. The range is start to
end as revision numbers: the youngest to 0 unless given, and revision 0 is
left out unless it has a message. The other arguments are those of
`svn-client-log`.

### svn-ra-get-dated-revision
The youngest revision that was committed at or before a date, given as a
string in the form `svn-time-to-cstring` writes or any other form
Subversion reads.

### svn-ra-rev-prop
The value of a revision property of a revision, or `nil`.

### svn-ra-rev-proplist
Every revision property of a revision, as `revprop` records.

### svn-ra-change-rev-prop
Sets a revision property, or deletes it for `nil`. Refused by a repository
without a `pre-revprop-change` hook, as `svn-client-revprop-set` is.

### svn-ra-has-capability
Whether the repository has a capability, named by an `SVN_RA_CAPABILITY_*`
symbol or by the string behind it: `SVN_RA_CAPABILITY_DEPTH`, `"depth"`.
A refusal for a name Subversion does not know.

### svn-repos-create
Makes a repository at a path, as `svnadmin create` does, and answers it as
a handle. The filesystem type is `fsfs` unless `"fsx"` is given, as that
string or as `SVN_FS_TYPE_FSX`. A refusal where a repository is already.
Freed with `svn-repos-free`.

### svn-repos-open
Opens the repository at a path.

### svn-repos-free
Gives a repository back.

### svn-repos-path
The absolute path of the repository.

### svn-repos-file-url
Not a Subversion function. The `file://` URL of the repository, which is
what the client functions take to reach it.

### svn-repos-hook-dir
The directory of the hook scripts: where a `pre-revprop-change` goes.

### svn-repos-conf-dir
The directory of the configuration files: `svnserve.conf`, `passwd`,
`authz`.

### svn-repos-fs-type
The filesystem type: `"fsfs"` or `"fsx"`.

### svn-repos-uuid
The uuid.

### svn-repos-set-uuid
Sets the uuid, to the one given or to a new one for `nil`. A working copy
of the repository is refused afterwards until it is relocated, because its
uuid no longer matches.

### svn-repos-youngest-rev
The youngest revision: 0 for a new repository.

### svn-repos-check-path
What is at a path in the repository in a revision, the youngest unless one
is given, as an `svn_node_*` symbol. The path is from the root of the
repository: `"trunk/README"`, with or without a leading slash; `""`, `"."`
or `nil` for the root.

### svn-repos-file-contents
The content of a file in a revision, as a string.

### svn-repos-dir-entries
The entries of a directory in a revision, as `dirent` records in name
order. The `path` of each is its name; `abs-path` is its path from the
root. The size, the revision it was last changed in, and the author and
date of that revision are filled from the filesystem.

### svn-repos-node-proplist
The properties of a path in a revision, as `prop` records.

### svn-repos-get-logs
The history of a path or a multifield of them from the root of the
repository, as `log` records, straight from the filesystem: no session,
no working copy. `nil` for the paths means the whole repository. The
arguments and the records are those of `svn-ra-get-log`.

### svn-repos-revprop-get
The value of a revision property, or `nil`. Straight from the filesystem:
no hook is consulted.

### svn-repos-revprop-set
Sets a revision property, or deletes it for `nil`, without the hooks, as
`svnadmin setrevprop` does. The optional author is recorded as the person
who made the change.

### svn-repos-revprop-list
Every revision property of a revision, as `revprop` records.

### svn-repos-recover
Recovers a repository at a path after an interrupted operation, as
`svnadmin recover` does. The repository must not be open elsewhere.

### svn-repos-verify
Verifies the revisions of the repository, from the start to the end given
or all of them, as `svnadmin verify` does. `TRUE` when every revision is
sound; the first problem is a refusal.

### svn-repos-pack
Packs the shards of the repository, as `svnadmin pack` does.

### svn-repos-hotcopy
Copies a repository at one path to another while it is in use, as
`svnadmin hotcopy` does. `TRUE` for incremental copies only what changed
into a copy made before.

### svn-repos-delete
Deletes the repository at a path.

### svn-repos-dump
Dumps the repository, from the start to the end revision given or all of
it, as `svnadmin dump` does: into a file, replaced if it exists, and
answers `TRUE`; or, for `nil` in place of the file, answers the dump as a
string. `TRUE` for incremental dumps the first revision as a difference
from the one before it; `TRUE` for use-deltas dumps every revision that
way.

### svn-repos-load
Loads a dump file into the repository, as `svnadmin load` does, under the
parent directory given, which must exist, or at the root.

### svn-records-kind
Not a Subversion function. The kind of the records in a list, as a
symbol: `status`, `log`, `changed-path`, `info`, `dirent`, `blame`,
`prop`, `revprop`, `diff-summary` or `notify`.

### svn-records-count
How many records the list has.

### svn-records-fields
The names of the fields of the records, as a multifield of symbols, in the
order `svn-record-byindex` uses.

### svn-record-byindex
Record number n, counted from zero, as a flat multifield of its values in
the order of the fields. `FALSE` past the end, without a message: a loop
that asks for 0, 1, 2 stops there.

### svn-record-field
One value of record number n, by the name of the field. `FALSE` past the
end, without a message; a refusal for a name the records do not have.

### svn-record-to-fact
Asserts record number n as a fact of the named deftemplate. Only the slots
with a field's name are filled, so a template with three of them is as good
as one with all. A multislot gets the value as one element, or as none for
`nil`. An identical fact that exists already is what CLIPS answers in place
of a new one. A refusal for a slot that will not take the value.

### svn-records-to-facts
Asserts every record as a fact of the named deftemplate, and answers how
many were new.

### svn-record-to-instance
Makes record number n an instance of the named defclass, with the name
given or one CLIPS makes up. The slots are filled as `svn-record-to-fact`
fills them.

### svn-records-to-instances
Makes every record an instance of the named defclass, and answers how many
it made.

### svn-records-free
Gives a list back. A list that belongs to another -- the changed paths of
a log -- is refused and goes with its owner.
