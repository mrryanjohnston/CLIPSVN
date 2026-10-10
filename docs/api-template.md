# CLIPSVN API Reference

Every user-defined function this library adds to the CLIPS environment,
grouped and ordered as it is registered. Each CLIPS function corresponds to
the Subversion function of the same name, spelled the way CLIPS spells
things: `svn_client_checkout3` is `svn-client-checkout`,
`svn_ra_get_latest_revnum` is `svn-ra-get-latest-revnum`. The version
suffix that the C API carries is dropped: the CLIPS function is the newest
form. The handful that have no C counterpart -- the ones that read a list
of records, or that turn one into a fact -- say so in their entry.

There are **@COUNT@** of them. This reference was generated against
**Subversion @SVN_VERSION@**; every function is present in every supported
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

<!-- functions -->

## Paths, URLs, times and words

<!-- functions -->

## Contexts

<!-- functions -->

## Working copies

<!-- functions -->

## Status, log, info, list, blame and diff

<!-- functions -->

## Merging

<!-- functions -->

## Properties

<!-- functions -->

## Repository access sessions

<!-- functions -->

## Repositories

<!-- functions -->

## Records

<!-- functions -->
