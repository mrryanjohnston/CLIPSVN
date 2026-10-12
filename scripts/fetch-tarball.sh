#!/bin/sh
# Downloads one source release and checks it against the digest that the
# makefile pins for it.
#
# The Apache Software Foundation publishes a digest for each release of APR
# and Subversion, and sqlite.org publishes one for each amalgamation. The
# makefile records those. A mismatch means that the file behind the URL is
# not those bytes. That is a reason to stop, whether the cause is a bad
# download or a replaced archive.
#
# The file is moved into place only after the check. So the next make cannot
# use an interrupted or damaged download as a good one.
#
# Usage:
#   scripts/fetch-tarball.sh <url> <sha256> <destination-file>
set -eu

url=${1:?usage: fetch-tarball.sh <url> <sha256> <dest-file>}
want=${2:?usage: fetch-tarball.sh <url> <sha256> <dest-file>}
dest=${3:?usage: fetch-tarball.sh <url> <sha256> <dest-file>}

die() { echo "fetch-tarball: $*" >&2; exit 1; }

sha256() {
    if command -v sha256sum >/dev/null 2>&1; then
        sha256sum "$1" | cut -d' ' -f1
    elif command -v shasum >/dev/null 2>&1; then
        shasum -a 256 "$1" | cut -d' ' -f1
    elif command -v openssl >/dev/null 2>&1; then
        openssl dgst -sha256 -r "$1" | cut -d' ' -f1
    else
        die "no SHA-256 tool found: need sha256sum, shasum or openssl"
    fi
}

fetch() {
    if command -v curl >/dev/null 2>&1; then
        curl -fsSL --connect-timeout 30 --retry 3 --retry-all-errors -o "$2" "$1"
    elif command -v wget >/dev/null 2>&1; then
        wget -q --timeout=30 --tries=3 -O "$2" "$1"
    else
        die "neither curl nor wget is installed"
    fi
}

mkdir -p "$(dirname "$dest")"
tmp="$dest.incoming.$$"
trap 'rm -f "$tmp"' EXIT INT TERM

echo "fetch-tarball: downloading $url"
fetch "$url" "$tmp" || die "download failed: $url"

got=$(sha256 "$tmp")
if [ "$got" != "$want" ]; then
    die "SHA-256 mismatch for $url
   expected $want
   actual   $got
   Nothing has been unpacked. Either the download was corrupted -- try
   again -- or the archive behind that URL is no longer the one this
   version was pinned to, which is worth understanding before building."
fi
echo "fetch-tarball: SHA-256 $got verified"

mv "$tmp" "$dest"
echo "fetch-tarball: $dest"
