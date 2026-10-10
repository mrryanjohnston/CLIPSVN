# CLIPSVN API Reference

Every user-defined function this library adds to the CLIPS environment,
grouped and ordered as it is registered. Each CLIPS function corresponds to
the Subversion function of the same name, spelled the way CLIPS spells
things: `svn_client_checkout3` is `svn-client-checkout`,
`svn_ra_get_latest_revnum` is `svn-ra-get-latest-revnum`. The version
suffix that the C API carries is dropped: the CLIPS function is the newest
form. The handful that have no C counterpart -- the ones that read a list
of records, or that turn one into a fact -- say so in their entry.

There are **138** of them. This reference was generated against
**Subversion 1.14.5**; every function is present in every supported
release, from 1.12 on.

## Conventions

**Handles.** A context, a repository access session, a repository, and a
list of records are CLIPS external addresses. Every one this library hands
out is recorded, with what kind of thing it is and what it belongs to, so:

- a handle of the wrong kind is refused by name -- passing a repository
  where a context belongs is a message, not a crash;
- a handle that has been freed is refused everywhere, and a second free is
  a refusal rather than a repeat;
- freeing a context frees the sessions that came from it first, because a
  session uses the authentication and configuration of its context;
- the changed paths of a log are *borrowed* from the log. They cannot be
  freed on their own and go when it goes.

`svn-handle-count` and `svn-handle-kinds` say what is still open.

**Refusals are values, not errors.** A wrapper that will not do something
writes a line naming itself and the reason to stderr and answers `FALSE`.
Nothing here raises a CLIPS error, which is what makes this the idiom
throughout:

```clips
(if (bind ?wc (svn-client-checkout ?ctx ?url "work"))
 then ...
 else (println "no checkout: " (svn-error-last)))
```

Since `FALSE` is also a perfectly good answer for the functions that return
a boolean, the ones where the difference matters say so in their entry.

**Records.** Subversion delivers a status, a log, a listing, a blame and the
rest through C callbacks, one item at a time. A CLIPS program cannot supply
a callback, so each of those functions collects the items into a list of
*records* and answers the list as a handle. Every record of a list has the
same fields; `svn-records-fields` names them, `svn-record-byindex` answers
one record as a flat multifield in that order, `svn-record-field` answers
one value by name, and `svn-record-to-fact` fills whichever slots of a
deftemplate have a field's name. The values are CLIPS values held by the
list, so a list outlives the context that made it and is read after that
context is freed. `svn-records-free` gives it back.

**Revisions.** Where Subversion takes a revision, an integer is a revision
number; the symbols `HEAD`, `BASE`, `COMMITTED`, `PREV` and `WORKING` are
what they are on the command line, as are the C names `svn_opt_revision_head`
and the rest; a string is parsed as the command line would parse it, so
`"{2026-01-01}"` is a date; and `nil` is unspecified, which each function
resolves as its entry says. A revision that comes back is an integer, or
`nil` where Subversion has none.

**Enumerations are symbols.** Everything Subversion names with a C constant
crosses as the symbol C spells it with -- `svn_node_file`,
`svn_depth_infinity`, `svn_wc_status_modified` -- and every one it takes is
accepted as that symbol, as the integer behind it, or, for a depth and a
conflict choice, as the word the command line uses: `infinity`,
`mine-full`.

**Paths and URLs.** A path is made absolute and canonical before Subversion
sees it, so a relative path is relative to the current directory of the
process, as on the command line. A URL is made canonical. Where a function
takes either, its argument is called a *target*. A function that takes
several -- a commit, an update, a lock -- takes one path or a multifield of
them.

**`nil` is the default.** An optional argument that is `nil` is one that
was not given: the default depth, the default revision, no log message. A
value that comes back as `nil` is one Subversion does not have: the author
of revision 0, the lock owner of a file that is not locked.

**Times.** A date crosses as the string Subversion writes,
`2026-09-05T12:34:56.123456Z`, and a record that has one also has the
same instant as seconds since the epoch, in the field of the same name with
`time` in place of `date`.

**Messages.** A committing function -- `svn-client-commit`,
`svn-client-mkdir` on a URL, `svn-client-copy` on a URL -- takes its log
message as an argument. There is no callback to answer.

## Not exposed

Deliberately left out, and why:

| | |
| --- | --- |
| Interactive authentication | A CLIPS program cannot answer a prompt for a password or a certificate. `svn-client-set-auth` gives a username and a password up front; the credentials cached on disk and the certificate files that the configuration names are used; nothing else can be asked for. |
| `http://` and `https://` in the vendored build | The vendored Subversion is built without Serf, so it reaches a repository by `file://` and `svn://`. A build against the system library (`make SVN_SYSTEM=1`) has whatever access methods the distribution built, usually those two as well. `svn-ra-modules` says what a build has. |
| Conflict resolution callbacks, `svn_client_conflict_*` | A conflict is postponed, as on the command line without `--accept`, and `svn-client-resolve` settles it afterwards with a choice. The interactive resolver has no place to land. |
| `svn_client_patch`, `svn_client_mergeinfo_*`, `svn_client_shelf_*`, changelists, externals | Not in this first release. The bookkeeping that makes the rest safe extends to them, and they are the natural next additions. |
| `svn_fs_*` transactions, `svn_delta_*`, `svn_wc_*` | The layers under the client API. A program that wants a commit makes it through a working copy or through `svn-client-mkdir`, `svn-client-copy` and their relatives on URLs. |
| `svn_ra_do_update`, `svn_ra_replay`, editors | They drive an editor with callbacks, which is how the client library is implemented, not how it is used. |
| Streams | `svn-client-cat`, `svn-ra-get-file`, `svn-repos-file-contents` and `svn-repos-dump` answer the whole content as a string, and `svn-repos-load` and `svn-repos-dump` take a file name. A string cannot hold a NUL byte, so a binary file is read up to its first one. |

## The library

### svn-version

```clips
(svn-version)
```

The version of the Subversion libraries loaded right now, as a string:
`"1.14.5"`.

**Returns:** string. This one takes no arguments and cannot be refused.

### svn-version-number

```clips
(svn-version-number)
```

The same version as a number: major times ten thousand, minor times a
hundred, plus the patch. 1.14.5 is `11405`.

**Returns:** integer. This one takes no arguments and cannot be refused.

### svn-built-version

```clips
(svn-built-version)
```

Not a Subversion function. The version of the Subversion the wrappers were
*compiled* against, in the same numbers. With the vendored build it is also
what is loaded, so the two agree; with `SVN_SYSTEM=1` they can differ.

**Returns:** integer. This one takes no arguments and cannot be refused.

### svn-version-tag

```clips
(svn-version-tag)
```

The tag of the loaded version -- `" (dev build)"`, `" (r1234)"` -- or
`nil` for a release.

**Returns:** string. This one takes no arguments and cannot be refused.

### svn-ra-modules

```clips
(svn-ra-modules)
```

What the loaded Subversion can reach a repository with, as the text that
`svn --version` prints: one line per module, `ra_local` for `file://`,
`ra_svn` for `svn://`, and `ra_serf` for `http://` and `https://` when the
build has it. The vendored build has the first two.

**Returns:** string, or `FALSE` if the call is refused.

### svn-fs-modules

```clips
(svn-fs-modules)
```

The repository filesystems the loaded Subversion has, in the same form:
`fs_fs` and `fs_x`.

**Returns:** string, or `FALSE` if the call is refused.

### svn-handle-count

```clips
(svn-handle-count)
```

Not a Subversion function. How many handles this library has handed out
and not yet had back. A program that frees what it takes ends with zero.

**Returns:** integer. This one takes no arguments and cannot be refused.

### svn-handle-kinds

```clips
(svn-handle-kinds)
```

Not a Subversion function. The kind of every open handle, as a multifield
of strings: `"context"`, `"ra session"`, `"repository"`, `"records"`. A
handle that belongs to another names it in parentheses, as
`"ra session (context)"` or `"records (records)"`. For finding what a
program forgot to free.

**Returns:** multifield. This one takes no arguments and cannot be refused.

### svn-error-last

```clips
(svn-error-last)
```

The message of the last error Subversion reported, or `nil` when there
has been none. An error is a chain of messages from the outside in, joined
with semicolons. Every refusal in this library writes the same message to
stderr as it happens, so this is for a program that wants to keep it. It
is not cleared by a call that succeeds: the refusal is what says there was
an error, and this is what it was.

**Returns:** string. This one takes no arguments and cannot be refused.

### svn-error-last-code

```clips
(svn-error-last-code)
```

The APR status code of that error, as an integer: `160006` for a revision
that does not exist. Zero when there has been none.

**Returns:** integer. This one takes no arguments and cannot be refused.

### svn-error-last-name

```clips
(svn-error-last-name)
```

The symbolic name of that code, as Subversion spells it:
`SVN_ERR_FS_NO_SUCH_REVISION`. `nil` when there has been none.

**Returns:** symbol. This one takes no arguments and cannot be refused.


## Paths, URLs, times and words

### svn-path-is-url

```clips
(svn-path-is-url ?text)
```

Whether the text is a URL: it has a scheme and `://`. `file:///tmp/r`
is one; `/tmp/r` is not.

**Returns:** boolean.

### svn-uri-canonicalize

```clips
(svn-uri-canonicalize ?url)
```

A URL in the one form Subversion uses: no trailing slash, the scheme and
host in lower case, a space as `%20`. Every URL that this library takes is
put through this first.

**Returns:** string, or `FALSE` if the call is refused.

### svn-dirent-canonicalize

```clips
(svn-dirent-canonicalize ?path)
```

A local path in the one form Subversion uses: no trailing slash, no double
slash. Every path that this library takes is put through this, and made
absolute, first.

**Returns:** string, or `FALSE` if the call is refused.

### svn-relpath-canonicalize

```clips
(svn-relpath-canonicalize ?path)
```

A path relative to something, in the one form Subversion uses: no leading
or trailing slash.

**Returns:** string, or `FALSE` if the call is refused.

### svn-dirent-get-absolute

```clips
(svn-dirent-get-absolute ?path)
```

A path made absolute against the current directory of the process, and
canonical.

**Returns:** string, or `FALSE` if the call is refused.

### svn-dirent-is-absolute

```clips
(svn-dirent-is-absolute ?path)
```

Whether a path is absolute.

**Returns:** boolean.

### svn-uri-is-canonical

```clips
(svn-uri-is-canonical ?url)
```

Whether a URL is already what `svn-uri-canonicalize` would make of it.

**Returns:** boolean.

### svn-dirent-is-canonical

```clips
(svn-dirent-is-canonical ?path)
```

The same for a path.

**Returns:** boolean.

### svn-uri-basename

```clips
(svn-uri-basename ?url)
```

The last component of a URL, decoded: `"c"` for `file:///a/b/c`.

**Returns:** string, or `FALSE` if the call is refused.

### svn-uri-dirname

```clips
(svn-uri-dirname ?url)
```

A URL with its last component removed.

**Returns:** string, or `FALSE` if the call is refused.

### svn-dirent-basename

```clips
(svn-dirent-basename ?path)
```

The last component of a path.

**Returns:** string, or `FALSE` if the call is refused.

### svn-dirent-dirname

```clips
(svn-dirent-dirname ?path)
```

A path with its last component removed.

**Returns:** string, or `FALSE` if the call is refused.

### svn-uri-join

```clips
(svn-uri-join ?url ?component)
```

A URL with a component added, encoded as it needs to be.

**Returns:** string, or `FALSE` if the call is refused.

### svn-dirent-join

```clips
(svn-dirent-join ?path ?component)
```

A path with a component added.

**Returns:** string, or `FALSE` if the call is refused.

### svn-uri-skip-ancestor

```clips
(svn-uri-skip-ancestor ?parent-url ?child-url)
```

The part of a URL below an ancestor URL, as a relative path: `"c/d"` for
`file:///a/b` and `file:///a/b/c/d`; `""` for the same URL twice; `nil`
when the first is not an ancestor of the second.

**Returns:** string, or `FALSE` if the call is refused.

### svn-dirent-skip-ancestor

```clips
(svn-dirent-skip-ancestor ?parent-path ?child-path)
```

The same for two paths.

**Returns:** string, or `FALSE` if the call is refused.

### svn-file-url-from-dirent

```clips
(svn-file-url-from-dirent ?path)
```

The `file://` URL of a local path, which is how a repository on this
machine is named to the client functions. The path is made absolute
first. `svn-repos-file-url` answers the same for an open repository.

**Returns:** string, or `FALSE` if the call is refused.

### svn-dirent-from-file-url

```clips
(svn-dirent-from-file-url ?url)
```

The local path of a `file://` URL. A refusal for a URL of another scheme.

**Returns:** string, or `FALSE` if the call is refused.

### svn-time-to-cstring

```clips
(svn-time-to-cstring ?seconds)
```

A time in seconds since the epoch, as the string Subversion writes
everywhere: `"2026-09-05T12:34:56.000000Z"`.

**Returns:** string, or `FALSE` if the call is refused.

### svn-time-from-cstring

```clips
(svn-time-from-cstring ?date)
```

The reverse: seconds since the epoch for a string in that form, or in any
other form Subversion reads. A refusal for text that is not a date.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-depth-to-word

```clips
(svn-depth-to-word ?depth)
```

The word the command line uses for a depth: `"infinity"` for
`svn_depth_infinity`. The word itself is accepted too.

**Returns:** string, or `FALSE` if the call is refused.

### svn-depth-from-word

```clips
(svn-depth-from-word ?word)
```

The depth for a word, as an `svn_depth_*` symbol; `svn_depth_unknown` for
a word that is not one.

**Returns:** symbol, or `FALSE` if the call is refused.

### svn-node-kind-to-word

```clips
(svn-node-kind-to-word ?kind)
```

The word for a node kind: `"dir"` for `svn_node_dir`.

**Returns:** string, or `FALSE` if the call is refused.

### svn-node-kind-from-word

```clips
(svn-node-kind-from-word ?word)
```

The node kind for a word, as an `svn_node_*` symbol.

**Returns:** symbol, or `FALSE` if the call is refused.

### svn-opt-parse-revision

```clips
(svn-opt-parse-revision ?text)
```

A revision or a revision range, parsed as the command line parses `-r`:
`"12"`, `"HEAD"`, `"{2026-01-01}"`, `"3:7"`. Answers a multifield of a
kind and a value for each revision: `(svn_opt_revision_number 12)`,
`(svn_opt_revision_head nil)`, and four elements for a range. A date
crosses as seconds since the epoch. A refusal for text that is neither.

**Returns:** multifield, or `FALSE` if the call is refused.


## Contexts

### svn-client-create-context

```clips
(svn-client-create-context [?config-dir])
```

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

**Arguments:** 0 to 1.

**Returns:** handle, or `FALSE` if the call is refused.

### svn-client-context-free

```clips
(svn-client-context-free ?ctx)
```

Gives a context back, and every session that came from it first. Each of
those handles is refused afterwards, and so is this one. The lists of
records that the context made are not affected: they belong to the
program.

**Returns:** boolean.

### svn-client-set-auth

```clips
(svn-client-set-auth ?ctx ?username [?password])
```

The username, and optionally the password, that the context answers a
challenge with. Over `file://` there is no challenge, and the username is
what a commit is recorded under. `nil` clears either. The authentication
remembers its first answer for a repository, so this replaces it: a
session that is already open keeps the old one.

**Arguments:** 2 to 3.

**Returns:** boolean.

### svn-client-set-config

```clips
(svn-client-set-config ?ctx ?section ?option ?value)
```

Sets one option of the `config` file of the context, in memory: the section
`"miscellany"`, the option `"global-ignores"`, the value. The file is not
written. `nil` for the value sets it to the empty string.

**Returns:** boolean.

### svn-client-get-config

```clips
(svn-client-get-config ?ctx ?section ?option)
```

The value of one option of that file, or `nil` when it is not set.

**Returns:** string, or `FALSE` if the call is refused.

### svn-client-notifications

```clips
(svn-client-notifications ?ctx)
```

Not a Subversion function. Every notification the last operation of the
context made, as a list of `notify` records: one for each path a commit
sent, each path an update changed, each path a status walk finished. The
fields are `action` (an `svn_wc_notify_*` symbol), `path`, `kind`,
`revision`, `content-state`, `prop-state`, `lock-state`, `url`,
`mime-type`, `changelist`, `error`, `old-revision` and `prop-name`. The
list is a copy: the context keeps its own until the next operation, and
the program frees this one with `svn-records-free`.

**Returns:** records, or `FALSE` if the call is refused.

### svn-client-notification-count

```clips
(svn-client-notification-count ?ctx)
```

How many notifications the last operation made, without the list.

**Returns:** integer, or `FALSE` if the call is refused.


## Working copies

### svn-client-checkout

```clips
(svn-client-checkout ?ctx ?url ?path [?revision] [?depth] [?ignore-externals])
```

Checks a URL out into a path, and answers the revision it checked out. The
revision defaults to `HEAD`; the depth to `infinity`; `TRUE` for the last
argument leaves externals out. The path is made if it is not there.

**Arguments:** 3 to 6.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-client-update

```clips
(svn-client-update ?ctx ?paths [?revision] [?depth] [?depth-is-sticky] [?ignore-externals])
```

Updates a path, or a multifield of paths, to a revision, `HEAD` unless
another is given, and answers the revision each is now at: an integer for
one path, a multifield for a multifield. The depth defaults to the depth
the working copy has; `TRUE` for depth-is-sticky makes the given depth the
working copy's; `TRUE` for the last argument leaves externals out.

**Arguments:** 2 to 6.

**Returns:** integer or multifield, or `FALSE` if the call is refused.

### svn-client-switch

```clips
(svn-client-switch ?ctx ?path ?url [?revision] [?depth] [?ignore-externals])
```

Switches a working copy path to another URL of the same repository -- a
branch, a tag -- and answers the revision. The revision defaults to
`HEAD`, the depth to `infinity`.

**Arguments:** 3 to 6.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-client-add

```clips
(svn-client-add ?ctx ?path [?depth] [?force] [?no-ignore] [?add-parents])
```

Schedules a path for addition. A directory is added with what is in it, to
the depth given, `infinity` unless another is. `TRUE` for force adds a path
that is added already without a refusal; `TRUE` for no-ignore adds what the
ignore patterns would skip; `TRUE` for add-parents adds the unversioned
directories above the path too.

**Arguments:** 2 to 6.

**Returns:** boolean.

### svn-client-mkdir

```clips
(svn-client-mkdir ?ctx ?paths [?make-parents] [?message])
```

Makes a directory, or each of a multifield of them. A path is scheduled in
the working copy and answers `TRUE`; a URL is committed at once with the
message, and answers the new revision. `TRUE` for make-parents makes the
directories above it as well.

**Arguments:** 2 to 4.

**Returns:** integer or TRUE, or `FALSE` if the call is refused.

### svn-client-delete

```clips
(svn-client-delete ?ctx ?paths [?force] [?keep-local] [?message])
```

Deletes a path or each of a multifield of them: scheduled in the working
copy, or committed at once for a URL, with the same answers as
`svn-client-mkdir`. `TRUE` for force deletes a path with local changes;
`TRUE` for keep-local schedules the deletion but leaves the file on disk.

**Arguments:** 2 to 5.

**Returns:** integer or TRUE, or `FALSE` if the call is refused.

### svn-client-copy

```clips
(svn-client-copy ?ctx ?source ?destination [?source-revision] [?copy-as-child] [?make-parents] [?message])
```

Copies a source, at a revision, to a destination. Working copy to working
copy schedules the copy; anything to a URL commits it with the message and
answers the revision. The revision defaults to the working version of a
path and `HEAD` of a URL. `TRUE` for copy-as-child puts the source inside a
destination that exists; `TRUE` for make-parents makes the directories
above the destination.

**Arguments:** 3 to 7.

**Returns:** integer or TRUE, or `FALSE` if the call is refused.

### svn-client-move

```clips
(svn-client-move ?ctx ?source ?destination [?move-as-child] [?make-parents] [?message])
```

Moves a source to a destination, with the same two forms and answers as
`svn-client-copy`.

**Arguments:** 3 to 6.

**Returns:** integer or TRUE, or `FALSE` if the call is refused.

### svn-client-commit

```clips
(svn-client-commit ?ctx ?paths ?message [?depth] [?keep-locks])
```

Commits a path or a multifield of them with the message, to the depth
given, and answers the new revision. `nil` when there was nothing to
commit. `TRUE` for keep-locks keeps the locks the commit would release.

**Arguments:** 3 to 5.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-client-revert

```clips
(svn-client-revert ?ctx ?paths [?depth])
```

Reverts a path or a multifield of them to the depth given, `empty` unless
another is: `infinity` reverts a tree.

**Arguments:** 2 to 3.

**Returns:** boolean.

### svn-client-resolve

```clips
(svn-client-resolve ?ctx ?path ?choice [?depth])
```

Marks a conflict resolved, with one of the choices the command line's
`--accept` takes: `svn_wc_conflict_choose_mine_full`, or the word
`mine-full`; `theirs-full`; `base`; `working` for what is in the working
copy now; `mine-conflict` and `theirs-conflict` for the conflicted regions
only. The depth defaults to `empty`.

**Arguments:** 3 to 4.

**Returns:** boolean.

### svn-client-cleanup

```clips
(svn-client-cleanup ?ctx ?path [?break-locks] [?fix-timestamps] [?vacuum-pristines] [?include-externals])
```

Cleans a working copy up: breaks the write locks it holds (`TRUE` unless
`FALSE` is given), fixes the recorded timestamps (`TRUE` unless `FALSE`),
removes the pristine copies nothing refers to, and does the same for
externals.

**Arguments:** 2 to 6.

**Returns:** boolean.

### svn-client-vacuum

```clips
(svn-client-vacuum ?ctx ?path [?remove-unversioned] [?remove-ignored] [?fix-timestamps] [?vacuum-pristines] [?include-externals])
```

Removes what a working copy does not need: unversioned items, ignored
items, the pristine copies nothing refers to. Each `FALSE` unless `TRUE`
is given, in that order, with fix-timestamps between the second and the
third and include-externals last.

**Arguments:** 2 to 7.

**Returns:** boolean.

### svn-client-relocate

```clips
(svn-client-relocate ?ctx ?path ?from-url ?to-url [?ignore-externals])
```

Points a working copy at a repository that moved: every URL in it that
starts with the from prefix has that replaced by the to prefix. The
repository must be reachable at the new place, so its identity can be
checked.

**Arguments:** 4 to 5.

**Returns:** boolean.

### svn-client-upgrade

```clips
(svn-client-upgrade ?ctx ?path)
```

Upgrades a working copy made by an older Subversion to the format this one
uses. Nothing to do for a current one.

**Returns:** boolean.

### svn-client-lock

```clips
(svn-client-lock ?ctx ?targets [?comment] [?steal])
```

Locks a path or a multifield of them in the repository, with an optional
comment. `TRUE` for steal takes a lock someone else holds.

**Arguments:** 2 to 4.

**Returns:** boolean.

### svn-client-unlock

```clips
(svn-client-unlock ?ctx ?targets [?break])
```

Unlocks them. `TRUE` for break removes a lock someone else holds.

**Arguments:** 2 to 3.

**Returns:** boolean.

### svn-client-export

```clips
(svn-client-export ?ctx ?source ?destination [?revision] [?overwrite] [?ignore-externals] [?depth])
```

Copies a tree out of a repository or a working copy into a directory,
without the `.svn` administrative area, and answers the revision. The
revision defaults to the working version of a path and `HEAD` of a URL.
`TRUE` for overwrite writes into a directory that exists.

**Arguments:** 3 to 7.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-client-import

```clips
(svn-client-import ?ctx ?path ?url ?message [?depth] [?no-ignore])
```

Commits an unversioned tree into a URL with the message, and answers the
new revision. The directory itself is not imported: what is in it goes
under the URL. `TRUE` for no-ignore imports what the ignore patterns would
skip.

**Arguments:** 4 to 6.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-client-cat

```clips
(svn-client-cat ?ctx ?target [?revision] [?peg-revision] [?expand-keywords])
```

The content of a file, as a string: from the working copy for a path,
from the repository for a URL, at the revision given or that default.
The peg revision, when given, is the revision the target is looked up in.
Keywords are expanded unless `FALSE` is given last. A string cannot hold a
NUL byte, so a binary file is read up to its first one.

**Arguments:** 2 to 5.

**Returns:** string, or `FALSE` if the call is refused.

### svn-client-get-wc-root

```clips
(svn-client-get-wc-root ?ctx ?path)
```

The root of the working copy a path is in.

**Returns:** string, or `FALSE` if the call is refused.

### svn-client-get-repos-root

```clips
(svn-client-get-repos-root ?ctx ?target)
```

The root URL and the uuid of the repository behind a path or a URL, as a
two-element multifield.

**Returns:** multifield, or `FALSE` if the call is refused.

### svn-client-url-from-path

```clips
(svn-client-url-from-path ?ctx ?target)
```

The URL a working copy path corresponds to, or a URL as it is.

**Returns:** string, or `FALSE` if the call is refused.

### svn-client-min-max-revisions

```clips
(svn-client-min-max-revisions ?ctx ?path [?committed])
```

The lowest and the highest revision in a working copy, as a two-element
multifield. A working copy where they differ is mixed-revision, which an
update settles. `TRUE` for committed asks for the last-changed revisions
instead.

**Arguments:** 2 to 3.

**Returns:** multifield, or `FALSE` if the call is refused.


## Status, log, info, list, blame and diff

### svn-client-status

```clips
(svn-client-status ?ctx ?path [?revision] [?depth] [?get-all] [?check-out-of-date] [?no-ignore] [?ignore-externals])
```

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

**Arguments:** 2 to 8.

**Returns:** records, or `FALSE` if the call is refused.

### svn-client-log

```clips
(svn-client-log ?ctx ?targets [?start] [?end] [?limit] [?discover-changed-paths] [?strict-node-history] [?peg-revision])
```

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

**Arguments:** 2 to 8.

**Returns:** records, or `FALSE` if the call is refused.

### svn-log-changed-paths

```clips
(svn-log-changed-paths ?log)
```

Not a Subversion function. The paths that the revisions of a log changed,
as a list of `changed-path` records: `revision`, `action` (the symbols
`A`, `D`, `M` and `R`), `path`, `kind`, `copyfrom-path`, `copyfrom-rev`,
`text-modified` and `props-modified` (`svn_tristate_*` symbols). The list
belongs to the log: it is refused by `svn-records-free` and goes when the
log does.

**Returns:** records, or `FALSE` if the call is refused.

### svn-client-info

```clips
(svn-client-info ?ctx ?target [?revision] [?peg-revision] [?depth])
```

Information about a path or a URL, and what is under it to the depth
given, `empty` unless another is, as a list of `info` records: `path`,
`url`, `revision`, `repos-root-url`, `repos-uuid`, `kind`, `size`,
`last-changed-rev`, `last-changed-date`, `last-changed-time`,
`last-changed-author`, `lock-owner`, `lock-token`, `lock-comment`, and
for a working copy path `schedule` (an `svn_wc_schedule_*` symbol),
`copyfrom-url`, `copyfrom-rev`, `depth`, `wc-root`, `changelist`,
`checksum`, `moved-from` and `moved-to`. With no revision, a working copy
path is answered from the working copy alone, without a repository access.

**Arguments:** 2 to 5.

**Returns:** records, or `FALSE` if the call is refused.

### svn-client-list

```clips
(svn-client-list ?ctx ?target [?revision] [?peg-revision] [?depth] [?fetch-locks])
```

A directory listing of a path or a URL, as a list of `dirent` records:
`path` (relative to the target, `.` for the target itself), `kind`,
`size`, `has-props`, `created-rev`, `date`, `time`, `author`,
`lock-owner`, `lock-token`, `lock-comment` and `abs-path`. The listing is
of the repository: at `HEAD` for a URL, at the base revision for a working
copy path, unless a revision is given. The depth defaults to `immediates`.
The locks are fetched only when the last argument is `TRUE`.

**Arguments:** 2 to 6.

**Returns:** records, or `FALSE` if the call is refused.

### svn-client-blame

```clips
(svn-client-blame ?ctx ?target [?start] [?end] [?peg-revision])
```

Who changed each line of a file, as a list of `blame` records, one per
line: `line-number` (from 1), `revision`, `author`, `date`, `time`,
`merged-revision`, `merged-path`, `merged-author`, `line` and
`local-change`. The start revision defaults to 1 and the end to the base
revision of a path or `HEAD` of a URL. A refusal for a binary file.

**Arguments:** 2 to 5.

**Returns:** records, or `FALSE` if the call is refused.

### svn-client-diff

```clips
(svn-client-diff ?ctx ?target1 ?revision1 ?target2 ?revision2 [?depth] [?ignore-ancestry] [?git-format])
```

The difference between two targets at two revisions, as the text of a
unified diff. Both revisions are required: `BASE` and `WORKING` for the
local changes of a working copy. The depth defaults to `infinity`; `TRUE`
for ignore-ancestry compares by path alone; `TRUE` for git-format writes
the header git writes.

**Arguments:** 5 to 8.

**Returns:** string, or `FALSE` if the call is refused.

### svn-client-diff-summarize

```clips
(svn-client-diff-summarize ?ctx ?target1 ?revision1 ?target2 ?revision2 [?depth] [?ignore-ancestry])
```

The same difference as a list of `diff-summary` records, one for each path
that differs: `path`, `summarize-kind` (an
`svn_client_diff_summarize_kind_*` symbol), `prop-changed` and
`node-kind`.

**Arguments:** 5 to 7.

**Returns:** records, or `FALSE` if the call is refused.


## Merging

### svn-client-merge

```clips
(svn-client-merge ?ctx ?source1 ?revision1 ?source2 ?revision2 ?target [?depth] [?ignore-ancestry] [?force-delete] [?record-only] [?dry-run])
```

Merges the difference between two sources at two revisions into a working
copy path: `svn merge source1@rev1 source2@rev2 target`. The depth
defaults to the working copy's; `TRUE` for ignore-ancestry compares by
path alone; `TRUE` for force-delete deletes files with local changes;
`TRUE` for record-only records the merge without making the changes;
`TRUE` for dry-run makes no changes at all. A mixed-revision working copy
is refused, as the command line refuses it: update first.

**Arguments:** 6 to 11.

**Returns:** boolean.

### svn-client-merge-peg

```clips
(svn-client-merge-peg ?ctx ?source ?target [?start] [?end] [?peg-revision] [?depth] [?ignore-ancestry] [?force-delete] [?record-only] [?dry-run])
```

Merges from one source into a working copy path: the revisions from start
to end when both are given, or everything eligible when neither is, which
is the automatic merge of `svn merge source target`. The peg revision is
the revision the source is looked up in. The other arguments are those of
`svn-client-merge`.

**Arguments:** 3 to 11.

**Returns:** boolean.


## Properties

### svn-client-propget

```clips
(svn-client-propget ?ctx ?name ?target [?revision] [?peg-revision])
```

The value of one property of a path or a URL, or `nil` when it is not
set. From the working copy for a path and `HEAD` for a URL, unless a
revision is given; the peg revision is the revision the target is looked
up in.

**Arguments:** 3 to 5.

**Returns:** string, or `FALSE` if the call is refused.

### svn-client-proplist

```clips
(svn-client-proplist ?ctx ?target [?revision] [?peg-revision] [?depth])
```

Every property of a path or a URL, and of what is under it to the depth
given, `empty` unless another is, as a list of `prop` records: `path`,
`name` and `value`, in name order.

**Arguments:** 2 to 5.

**Returns:** records, or `FALSE` if the call is refused.

### svn-client-propset-local

```clips
(svn-client-propset-local ?ctx ?name ?value ?targets [?depth] [?skip-checks])
```

Sets a property on a path or a multifield of them, in the working copy,
to the depth given. `nil` for the value deletes the property. A value
Subversion does not accept for an `svn:` property is a refusal, unless
skip-checks is `TRUE`.

**Arguments:** 4 to 6.

**Returns:** boolean.

### svn-client-propset-remote

```clips
(svn-client-propset-remote ?ctx ?name ?value ?url ?base-revision [?message])
```

Sets a property on a URL, committed at once with the message, and answers
the new revision. The base revision is the revision the URL is expected to
be at, and the commit is refused if it has changed since. `nil` for the
value deletes the property.

**Arguments:** 5 to 6.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-client-revprop-get

```clips
(svn-client-revprop-get ?ctx ?name ?url ?revision)
```

The value of a revision property -- `svn:log`, `svn:author`, `svn:date`,
or a custom one -- of a revision of the repository at a URL, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### svn-client-revprop-set

```clips
(svn-client-revprop-set ?ctx ?name ?value ?url ?revision [?force])
```

Sets a revision property, and answers the revision. `nil` for the value
deletes it; `TRUE` for force sets `svn:log` and its relatives without
checking the value. A repository refuses a change to a revision property
unless it has a `pre-revprop-change` hook that allows it: a script by that
name in `svn-repos-hook-dir`. `svn-repos-revprop-set` bypasses the hook.

**Arguments:** 5 to 6.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-client-revprop-list

```clips
(svn-client-revprop-list ?ctx ?url ?revision)
```

Every revision property of a revision, as a list of `revprop` records:
`name` and `value`, in name order.

**Returns:** records, or `FALSE` if the call is refused.


## Repository access sessions

### svn-client-open-ra-session

```clips
(svn-client-open-ra-session ?ctx ?url)
```

A session with the repository at a URL, for the `svn-ra-*` functions: the
repository read directly, without a working copy. The session uses the
authentication and configuration of the context, belongs to it, and is
freed with `svn-ra-session-free` or with the context.

**Returns:** handle, or `FALSE` if the call is refused.

### svn-ra-session-free

```clips
(svn-ra-session-free ?session)
```

Gives a session back.

**Returns:** boolean.

### svn-ra-get-latest-revnum

```clips
(svn-ra-get-latest-revnum ?session)
```

The youngest revision of the repository.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-ra-get-uuid

```clips
(svn-ra-get-uuid ?session)
```

The uuid of the repository.

**Returns:** string, or `FALSE` if the call is refused.

### svn-ra-get-repos-root

```clips
(svn-ra-get-repos-root ?session)
```

The root URL of the repository, which the session URL is at or under.

**Returns:** string, or `FALSE` if the call is refused.

### svn-ra-get-session-url

```clips
(svn-ra-get-session-url ?session)
```

The URL the session is on: the one it was opened on, or the one
`svn-ra-reparent` moved it to.

**Returns:** string, or `FALSE` if the call is refused.

### svn-ra-reparent

```clips
(svn-ra-reparent ?session ?url)
```

Moves the session to another URL of the same repository. Every path
argument is relative to the session URL, so this is how a session is
pointed at a subdirectory.

**Returns:** boolean.

### svn-ra-check-path

```clips
(svn-ra-check-path ?session ?path [?revision])
```

What is at a path relative to the session URL -- `""` or `nil` for the
session URL itself -- in a revision, the youngest unless one is given, as
an `svn_node_*` symbol: `svn_node_none` for nothing.

**Arguments:** 2 to 3.

**Returns:** symbol, or `FALSE` if the call is refused.

### svn-ra-stat

```clips
(svn-ra-stat ?session ?path [?revision])
```

A `dirent` record for a path in a revision, in a list of one, or of none
when there is nothing at the path. The fields are those of
`svn-client-list`.

**Arguments:** 2 to 3.

**Returns:** records, or `FALSE` if the call is refused.

### svn-ra-get-file

```clips
(svn-ra-get-file ?session ?path [?revision])
```

The content of a file at a path in a revision, as a string.

**Arguments:** 2 to 3.

**Returns:** string, or `FALSE` if the call is refused.

### svn-ra-get-file-props

```clips
(svn-ra-get-file-props ?session ?path [?revision])
```

The properties of a file at a path in a revision, as `prop` records. The
entry properties Subversion adds -- `svn:entry:committed-rev` and its
relatives -- are among them.

**Arguments:** 2 to 3.

**Returns:** records, or `FALSE` if the call is refused.

### svn-ra-get-dir

```clips
(svn-ra-get-dir ?session ?path [?revision])
```

The entries of a directory at a path in a revision, as `dirent` records
in name order. The `path` of each is relative to the session URL.

**Arguments:** 2 to 3.

**Returns:** records, or `FALSE` if the call is refused.

### svn-ra-get-log

```clips
(svn-ra-get-log ?session ?paths [?start] [?end] [?limit] [?discover-changed-paths] [?strict-node-history])
```

The history of a path, or of a multifield of them, relative to the session
URL -- `""` for the session URL itself -- as `log` records, with the same
fields and the same second list as `svn-client-log`. The range is start to
end as revision numbers: the youngest to 0 unless given, and revision 0 is
left out unless it has a message. The other arguments are those of
`svn-client-log`.

**Arguments:** 2 to 7.

**Returns:** records, or `FALSE` if the call is refused.

### svn-ra-get-dated-revision

```clips
(svn-ra-get-dated-revision ?session ?date)
```

The youngest revision that was committed at or before a date, given as a
string in the form `svn-time-to-cstring` writes or any other form
Subversion reads.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-ra-rev-prop

```clips
(svn-ra-rev-prop ?session ?revision ?name)
```

The value of a revision property of a revision, or `nil`.

**Returns:** string, or `FALSE` if the call is refused.

### svn-ra-rev-proplist

```clips
(svn-ra-rev-proplist ?session ?revision)
```

Every revision property of a revision, as `revprop` records.

**Returns:** records, or `FALSE` if the call is refused.

### svn-ra-change-rev-prop

```clips
(svn-ra-change-rev-prop ?session ?revision ?name ?value)
```

Sets a revision property, or deletes it for `nil`. Refused by a repository
without a `pre-revprop-change` hook, as `svn-client-revprop-set` is.

**Returns:** boolean.

### svn-ra-has-capability

```clips
(svn-ra-has-capability ?session ?capability)
```

Whether the repository has a capability, named by an `SVN_RA_CAPABILITY_*`
symbol or by the string behind it: `SVN_RA_CAPABILITY_DEPTH`, `"depth"`.
A refusal for a name Subversion does not know.

**Returns:** boolean.


## Repositories

### svn-repos-create

```clips
(svn-repos-create ?path [?fs-type])
```

Makes a repository at a path, as `svnadmin create` does, and answers it as
a handle. The filesystem type is `fsfs` unless `"fsx"` is given, as that
string or as `SVN_FS_TYPE_FSX`. A refusal where a repository is already.
Freed with `svn-repos-free`.

**Arguments:** 1 to 2.

**Returns:** handle, or `FALSE` if the call is refused.

### svn-repos-open

```clips
(svn-repos-open ?path)
```

Opens the repository at a path.

**Returns:** handle, or `FALSE` if the call is refused.

### svn-repos-free

```clips
(svn-repos-free ?repos)
```

Gives a repository back.

**Returns:** boolean.

### svn-repos-path

```clips
(svn-repos-path ?repos)
```

The absolute path of the repository.

**Returns:** string, or `FALSE` if the call is refused.

### svn-repos-file-url

```clips
(svn-repos-file-url ?repos)
```

Not a Subversion function. The `file://` URL of the repository, which is
what the client functions take to reach it.

**Returns:** string, or `FALSE` if the call is refused.

### svn-repos-hook-dir

```clips
(svn-repos-hook-dir ?repos)
```

The directory of the hook scripts: where a `pre-revprop-change` goes.

**Returns:** string, or `FALSE` if the call is refused.

### svn-repos-conf-dir

```clips
(svn-repos-conf-dir ?repos)
```

The directory of the configuration files: `svnserve.conf`, `passwd`,
`authz`.

**Returns:** string, or `FALSE` if the call is refused.

### svn-repos-fs-type

```clips
(svn-repos-fs-type ?repos)
```

The filesystem type: `"fsfs"` or `"fsx"`.

**Returns:** string, or `FALSE` if the call is refused.

### svn-repos-uuid

```clips
(svn-repos-uuid ?repos)
```

The uuid.

**Returns:** string, or `FALSE` if the call is refused.

### svn-repos-set-uuid

```clips
(svn-repos-set-uuid ?repos [?uuid])
```

Sets the uuid, to the one given or to a new one for `nil`. A working copy
of the repository is refused afterwards until it is relocated, because its
uuid no longer matches.

**Arguments:** 1 to 2.

**Returns:** boolean.

### svn-repos-youngest-rev

```clips
(svn-repos-youngest-rev ?repos)
```

The youngest revision: 0 for a new repository.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-repos-check-path

```clips
(svn-repos-check-path ?repos ?path [?revision])
```

What is at a path in the repository in a revision, the youngest unless one
is given, as an `svn_node_*` symbol. The path is from the root of the
repository: `"trunk/README"`, with or without a leading slash; `""`, `"."`
or `nil` for the root.

**Arguments:** 2 to 3.

**Returns:** symbol, or `FALSE` if the call is refused.

### svn-repos-file-contents

```clips
(svn-repos-file-contents ?repos ?path [?revision])
```

The content of a file in a revision, as a string.

**Arguments:** 2 to 3.

**Returns:** string, or `FALSE` if the call is refused.

### svn-repos-dir-entries

```clips
(svn-repos-dir-entries ?repos ?path [?revision])
```

The entries of a directory in a revision, as `dirent` records in name
order. The `path` of each is its name; `abs-path` is its path from the
root. The size, the revision it was last changed in, and the author and
date of that revision are filled from the filesystem.

**Arguments:** 2 to 3.

**Returns:** records, or `FALSE` if the call is refused.

### svn-repos-node-proplist

```clips
(svn-repos-node-proplist ?repos ?path [?revision])
```

The properties of a path in a revision, as `prop` records.

**Arguments:** 2 to 3.

**Returns:** records, or `FALSE` if the call is refused.

### svn-repos-get-logs

```clips
(svn-repos-get-logs ?repos ?paths [?start] [?end] [?limit] [?discover-changed-paths] [?strict-node-history])
```

The history of a path or a multifield of them from the root of the
repository, as `log` records, straight from the filesystem: no session,
no working copy. `nil` for the paths means the whole repository. The
arguments and the records are those of `svn-ra-get-log`.

**Arguments:** 2 to 7.

**Returns:** records, or `FALSE` if the call is refused.

### svn-repos-revprop-get

```clips
(svn-repos-revprop-get ?repos ?revision ?name)
```

The value of a revision property, or `nil`. Straight from the filesystem:
no hook is consulted.

**Returns:** string, or `FALSE` if the call is refused.

### svn-repos-revprop-set

```clips
(svn-repos-revprop-set ?repos ?revision ?name ?value [?author])
```

Sets a revision property, or deletes it for `nil`, without the hooks, as
`svnadmin setrevprop` does. The optional author is recorded as the person
who made the change.

**Arguments:** 4 to 5.

**Returns:** boolean.

### svn-repos-revprop-list

```clips
(svn-repos-revprop-list ?repos ?revision)
```

Every revision property of a revision, as `revprop` records.

**Returns:** records, or `FALSE` if the call is refused.

### svn-repos-recover

```clips
(svn-repos-recover ?path)
```

Recovers a repository at a path after an interrupted operation, as
`svnadmin recover` does. The repository must not be open elsewhere.

**Returns:** boolean.

### svn-repos-verify

```clips
(svn-repos-verify ?repos [?start] [?end])
```

Verifies the revisions of the repository, from the start to the end given
or all of them, as `svnadmin verify` does. `TRUE` when every revision is
sound; the first problem is a refusal.

**Arguments:** 1 to 3.

**Returns:** boolean.

### svn-repos-pack

```clips
(svn-repos-pack ?repos)
```

Packs the shards of the repository, as `svnadmin pack` does.

**Returns:** boolean.

### svn-repos-hotcopy

```clips
(svn-repos-hotcopy ?source ?destination [?incremental])
```

Copies a repository at one path to another while it is in use, as
`svnadmin hotcopy` does. `TRUE` for incremental copies only what changed
into a copy made before.

**Arguments:** 2 to 3.

**Returns:** boolean.

### svn-repos-delete

```clips
(svn-repos-delete ?path)
```

Deletes the repository at a path.

**Returns:** boolean.

### svn-repos-dump

```clips
(svn-repos-dump ?repos ?file [?start] [?end] [?incremental] [?use-deltas])
```

Dumps the repository, from the start to the end revision given or all of
it, as `svnadmin dump` does: into a file, replaced if it exists, and
answers `TRUE`; or, for `nil` in place of the file, answers the dump as a
string. `TRUE` for incremental dumps the first revision as a difference
from the one before it; `TRUE` for use-deltas dumps every revision that
way.

**Arguments:** 2 to 6.

**Returns:** string or TRUE, or `FALSE` if the call is refused.

### svn-repos-load

```clips
(svn-repos-load ?repos ?file [?parent-dir])
```

Loads a dump file into the repository, as `svnadmin load` does, under the
parent directory given, which must exist, or at the root.

**Arguments:** 2 to 3.

**Returns:** boolean.


## Records

### svn-records-kind

```clips
(svn-records-kind ?records)
```

Not a Subversion function. The kind of the records in a list, as a
symbol: `status`, `log`, `changed-path`, `info`, `dirent`, `blame`,
`prop`, `revprop`, `diff-summary` or `notify`.

**Returns:** symbol, or `FALSE` if the call is refused.

### svn-records-count

```clips
(svn-records-count ?records)
```

How many records the list has.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-records-fields

```clips
(svn-records-fields ?records)
```

The names of the fields of the records, as a multifield of symbols, in the
order `svn-record-byindex` uses.

**Returns:** multifield, or `FALSE` if the call is refused.

### svn-record-byindex

```clips
(svn-record-byindex ?records ?index)
```

Record number n, counted from zero, as a flat multifield of its values in
the order of the fields. `FALSE` past the end, without a message: a loop
that asks for 0, 1, 2 stops there.

**Returns:** multifield, or `FALSE` if the call is refused.

### svn-record-field

```clips
(svn-record-field ?records ?index ?field)
```

One value of record number n, by the name of the field. `FALSE` past the
end, without a message; a refusal for a name the records do not have.

**Returns:** value, or `FALSE` if the call is refused.

### svn-record-to-fact

```clips
(svn-record-to-fact ?records ?index ?deftemplate)
```

Asserts record number n as a fact of the named deftemplate. Only the slots
with a field's name are filled, so a template with three of them is as good
as one with all. A multislot gets the value as one element, or as none for
`nil`. An identical fact that exists already is what CLIPS answers in place
of a new one. A refusal for a slot that will not take the value.

**Returns:** fact, or `FALSE` if the call is refused.

### svn-records-to-facts

```clips
(svn-records-to-facts ?records ?deftemplate)
```

Asserts every record as a fact of the named deftemplate, and answers how
many were new.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-record-to-instance

```clips
(svn-record-to-instance ?records ?index ?defclass [?name])
```

Makes record number n an instance of the named defclass, with the name
given or one CLIPS makes up. The slots are filled as `svn-record-to-fact`
fills them.

**Arguments:** 3 to 4.

**Returns:** instance, or `FALSE` if the call is refused.

### svn-records-to-instances

```clips
(svn-records-to-instances ?records ?defclass)
```

Makes every record an instance of the named defclass, and answers how many
it made.

**Returns:** integer, or `FALSE` if the call is refused.

### svn-records-free

```clips
(svn-records-free ?records)
```

Gives a list back. A list that belongs to another -- the changed paths of
a log -- is refused and goes with its owner.

**Returns:** boolean.

