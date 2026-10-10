# CLIPSVN

CLIPSVN is a Subversion library for CLIPS. It gives CLIPS programs access to
the [Subversion](https://subversion.apache.org) libraries: working copies,
repository access sessions, and repositories.

```clips
(deftemplate revision (slot revision) (slot author) (slot message) (slot changed-path-count))

(defrule large-commit
  (revision (revision ?r) (message ?m) (changed-path-count ?n&:(>= ?n 3)))
  =>
  (println "r" ?r " touched " ?n " paths: " ?m))

(bind ?ctx (svn-client-create-context))
(bind ?log (svn-client-log ?ctx "file:///srv/repos/project"))
(svn-records-to-facts ?log revision)
(svn-records-free ?log)
(svn-client-context-free ?ctx)
(run)
```

[API.md](API.md) lists every function. The [examples](examples/) directory
has complete example programs.

CLIPSVN supports **Subversion 1.14**. The build links Subversion, APR and
APR-util statically. The binary does not need an installed Subversion.

## Requirements

You need these tools:

- `curl` or `wget`
- `tar` with `bzip2`
- a C compiler and `make`
- the zlib and Expat headers, which APR-util and Subversion need. On Debian
  and Ubuntu, install `zlib1g-dev` and `libexpat1-dev`. On Fedora, install
  `zlib-devel` and `expat-devel`. On macOS, both come with the command line
  tools.

A build against a CLIPS development branch also needs `svn`. The default
build does not.

```
sudo apt-get install zlib1g-dev libexpat1-dev
```

## Build

```
make
```

The default build does these steps:

1. It downloads the pinned releases of APR, APR-util, SQLite, and
   Subversion.
2. It checks each download against the SHA-256 digest that the makefile
   records.
3. It builds them as static libraries in `vendor/`.
4. It downloads CLIPS 6.4.2.
5. It builds a binary that contains the `svn-*` functions.

Each CLIPS version is fetched and built in its own directory. `vendor/clips`
is a symlink to the version that was built last. So after a plain `make`,
the binary is `vendor/clips/clips`. `make print-clips` shows which version
that is.

The Subversion build is small. SQLite is the amalgamation, compiled in. LZ4
and utf8proc are the copies that Subversion bundles. There is no Serf, so
the vendored build reaches a repository by `file://` and `svn://`. A build
against the system library has `http://` and `https://` as well. The build
takes a few minutes.

To fetch and build Subversion without CLIPS:

```
make subversion
```

Other `make` variables:

| Variable | Default | Meaning |
| --- | --- | --- |
| `SVN_VERSION` | `1.14.5` | The Subversion release to download and build |
| `SVN_SHA256` | the digest of that release | The makefile checks the digest before it unpacks the tarball |
| `SVN_SYSTEM` | not set | Set `1` to link the Subversion that is installed on this machine |
| `SVN_CONFIGURE_OPTS` | see the makefile | The configure options for the vendored build |
| `APR_VERSION`, `APU_VERSION`, `SQLITE_VERSION` | `1.7.6`, `1.6.3`, `3530400` | The releases of the libraries under Subversion |
| `NPROC` | the number of processors | The parallelism of the vendored build |
| `CLIPS_VERSION` | `6.4.2` | The CLIPS to build against: `6.4.2`, `svn-6x`, or `svn-7x` |
| `CLIPS_SVN_REV` | the pinned revision | The revision of a branch to build, or `HEAD` |
| `PREFIX` | `$(HOME)/.local` | The install root for `make install` |
| `VERSION` | `0.1.0` | The version of this library, used in the installed file names |

`make help` lists the targets.

### Other CLIPS versions

CLIPSVN builds against the CLIPS 6.4.2 release and against the two
development branches:

```
make                          # 6.4.2, the release tarball (the default)
make CLIPS_VERSION=svn-6x     # branches/64x, the 6.4 maintenance branch
make CLIPS_VERSION=svn-7x     # branches/70x, which adds deftable and goals
```

A branch changes over time. So each branch is **pinned to a revision**. A
build today and a build next month give the same result. The pins are
`CLIPS_SVN_6X_REV` and `CLIPS_SVN_7X_REV` in the makefile. To build a branch
at a different revision, or at the current head:

```
make CLIPS_VERSION=svn-7x CLIPS_SVN_REV=980
make CLIPS_VERSION=svn-7x CLIPS_SVN_REV=HEAD
```

The branches come from Subversion. A branch build needs `svn`
(`sudo apt install subversion` or `brew install subversion`). The default
build does not.

Each version is fetched into `vendor/clips-source/<tag>` and built in
`vendor/clips-build/<tag>`. A change to a different version does not rebuild
the version that you leave. `make clips-source` fetches the selected version
without a build.

```
make test-clips          # the complete suite against all three versions, one after the other
```

### Other Subversion versions

```
make SVN_VERSION=1.14.3
```

The makefile has a digest for these versions. The library is tested against
them:

| | | |
| --- | --- | --- |
| 1.14.3 | 1.14.4 | 1.14.5 |

The Apache Software Foundation publishes a digest for each release. The
makefile records those. To build a version that is not in the table, give
its digest:

```
make SVN_VERSION=1.14.6 SVN_SHA256=<the digest apache.org publishes>
```

Each version is built and installed in its own directory in `vendor/`, with
its own APR. `make test-versions` runs the complete suite against each
version in turn.

The wrappers need Subversion 1.12 or later. The 1.14 series is what they are
built and tested against.

- `svn-built-version` is the Subversion version that the wrappers were
  compiled against. That version decides what is in the binary.
- `svn-version-number` is the version of the library that is loaded now.
  With the static build, the two are the same. With `SVN_SYSTEM=1`, the
  two can be different.

### Build against the system library

```
sudo apt-get install libsvn-dev libapr1-dev libaprutil1-dev pkg-config && make SVN_SYSTEM=1
brew install subversion && make SVN_SYSTEM=1
```

The makefile asks `pkg-config`, `apr-1-config` and `apu-1-config` for the
location of the headers and the libraries. The system library is linked
dynamically. So the installed binary needs that library to remain installed.
The system library has whatever access methods the distribution built,
usually `http://` and `https://` among them. `svn-ra-modules` reports what a
build has.

## Test

```
make test
```

`tests/run.sh` runs the in-process suite and then the examples. It checks
each example against the `.expected` file next to it. The tests make every
repository and working copy that they use under `tests/tmp` or
`examples/tmp`, and they use a configuration directory under `tests/tmp`, so
the `~/.subversion` of this machine plays no part. The tests do not need an
installed svn, a network, or one of your repositories.

| | |
| --- | --- |
| `make test` | the complete suite and the examples |
| `make test-suite` | only the in-process suite |
| `make test-examples` | only the examples |
| `make test-valgrind` | the suite under valgrind |
| `make test-versions` | the suite against every supported Subversion |
| `make test-clips` | the suite against all three CLIPS versions |
| `make coverage` | line coverage of `userfunctions.c` |
| `make docs-check` | fails if `API.md` is not current |

## Install

```
make install
```

This installs into `$HOME/.local` and needs no special permissions. The real
binary goes into `libexec`. A small wrapper script on `PATH` starts it.
Subversion is inside the binary, so the installed copy does not need
`vendor/`.

For a system-wide location, give the prefix:

```
sudo make install PREFIX=/usr/local
```

You must give the prefix. `sudo` resets `HOME`, so `sudo make install`
without a `PREFIX` would install into `/root/.local`. The makefile rejects
that command.

`make uninstall` removes the installed files.

## License

MIT. See [LICENSE](LICENSE).
