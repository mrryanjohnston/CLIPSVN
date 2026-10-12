#!/bin/sh
# Fetches one CLIPS core source tree into a directory.
#
# A tree can come from two places, because CLIPSVN is built and tested
# against three trees: the 6.4.2 release tarball on SourceForge, and the 64x
# and 70x branches of the CLIPS Subversion repository.
#
# Usage:
#   scripts/fetch-clips.sh tarball <url> <archive-path> <dest-dir>
#   scripts/fetch-clips.sh svn     <url> <revision>     <dest-dir>
#
# A Subversion revision is pinned. Then a build today and a build next month
# give the same result. Give HEAD to get the current revision. See the note at
# the end of this file about the effect of HEAD.
#
# Environment:
#   CLIPS_SHA256   The SHA-256 digest that the tarball must have. The
#                  makefile gives the digest that is published for 6.4.2. A
#                  release does not change. So a mismatch means a damaged
#                  download, or a file that is not the one this project was
#                  tested against. An empty value skips the check. A branch
#                  export uses an empty value, because a branch has no
#                  published digest.
#
# On success, the tree is in <dest-dir>, and <dest-dir>/.clips-source
# identifies it.
set -eu

kind=${1:?usage: fetch-clips.sh <tarball|svn> <url> <archive-path|revision> <dest-dir>}
url=${2:?usage: fetch-clips.sh <tarball|svn> <url> <archive-path|revision> <dest-dir>}
third=${3:?usage: fetch-clips.sh <tarball|svn> <url> <archive-path|revision> <dest-dir>}
dest=${4:?usage: fetch-clips.sh <tarball|svn> <url> <archive-path|revision> <dest-dir>}

want=${CLIPS_SHA256:-}

die() { echo "fetch-clips: $*" >&2; exit 1; }

fetch() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --connect-timeout 30 --retry 3 --retry-all-errors -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --timeout=30 --tries=3 -O "$2" "$1"
    else
        die "neither curl nor wget is installed"
    fi
}

# SourceForge answers a download with a redirect to a mirror that it picks by
# location, and it picks the same mirror again on a retry. So when that
# mirror is down, the download fails however often it is tried, and other
# mirrors are asked for by name.
download() {
    fetch "$1" "$2" && return 0
    case "$1" in
    *sourceforge.net/*)
        for mirror in netactuate pilotfiber gigenet; do
            echo "fetch-clips: the download failed, trying the $mirror mirror"
            rm -f "$2"
            fetch "$1/download?use_mirror=$mirror" "$2" && return 0
        done
        ;;
    esac
    return 1
}

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    else
        die "no SHA-256 tool found: need sha256sum or shasum"
    fi
}

# A tree that is not complete is worse than no tree, because make would use it
# as a complete tree. So each result is made next to the destination, and is
# moved into place only when it is complete.
staging="$dest.incoming"
rm -rf "$staging"
mkdir -p "$staging"
trap 'rm -rf "$staging"' EXIT INT TERM

case "$kind" in
tarball)
    archive=$third
    if [ ! -f "$archive" ]; then
        echo "fetch-clips: downloading $url"
        mkdir -p "$(dirname "$archive")"
        download "$url" "$archive.incoming" || die "download failed: $url"
        mv "$archive.incoming" "$archive"
    else
        echo "fetch-clips: using the archive already at $archive"
    fi

    # The digest is checked on the cached archive and on a new download. So a
    # file that became damaged on disk is also found.
    if [ -n "$want" ]; then
        got=$(sha256 "$archive")
        if [ "$got" != "$want" ]; then
            die "SHA-256 mismatch for $archive
   expected $want
   actual   $got
   Nothing has been unpacked. Either the download was corrupted -- remove
   that file and build again -- or the file SourceForge is serving is no
   longer the one this project was tested against, which is worth
   understanding before building."
        fi
        echo "fetch-clips: SHA-256 $got verified"
    fi

    # clips_core_source_642/core/*.c -> *.c
    tar --strip-components=2 -xf "$archive" -C "$staging" \
        || die "could not unpack $archive"
    description="tarball $url"
    ;;

svn)
    revision=$third
    command -v svn >/dev/null 2>&1 || die "svn is not installed: it is needed
   to build against a CLIPS branch. On Ubuntu-based systems that is
   'sudo apt install subversion'; on macOS, 'brew install subversion'.
   Building against the 6.4.2 release tarball instead needs no svn:
   run make with CLIPS_VERSION=6.4.2, which is the default."

    echo "fetch-clips: exporting $url at r$revision"
    svn export -q --force -r "$revision" "$url" "$staging" \
        || die "svn export failed: $url at r$revision"

    # HEAD changes over time. So record the revision that it gave.
    revision=$(svn info --show-item last-changed-revision -r "$revision" "$url" 2>/dev/null) \
        || revision=$third
    description="svn $url at r$revision"
    ;;

*)
    die "unknown source kind '$kind': expected tarball or svn"
    ;;
esac

# The build uses the makefile that CLIPS supplies. A tree without that
# makefile is not usable here, even if it is complete in other ways.
[ -f "$staging/makefile" ] || die "no makefile in the tree fetched from $url"

printf '%s\n' "$description" > "$staging/.clips-source"

rm -rf "$dest"
mkdir -p "$(dirname "$dest")"
mv "$staging" "$dest"
trap - EXIT INT TERM

echo "fetch-clips: $description -> $dest"

# A note on HEAD: the directory of a tree is named after the revision that you
# asked for. So CLIPS_SVN_REV=HEAD reuses the revision that HEAD gave the
# first time. Remove that directory to fetch HEAD again.
