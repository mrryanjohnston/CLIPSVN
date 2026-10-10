#!/bin/sh
# Builds and tests against every Subversion release that the makefile pins,
# one after the other. Each release is fetched, built, and installed in its
# own directory in vendor/. A release that is already built there is reused.
#
# Usage:
#   scripts/test-versions.sh
#   SVN_VERSIONS="1.14.3 1.14.5" scripts/test-versions.sh
set -u

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$root" || exit 1

versions=${SVN_VERSIONS:-$(make -s print-svn-versions)}

failed=
for v in $versions; do
    echo
    echo "=== Subversion $v ==="
    if ! make --no-print-directory SVN_VERSION="$v"; then
        failed="$failed $v"
        continue
    fi
    if ! make --no-print-directory SVN_VERSION="$v" test; then
        failed="$failed $v"
    fi
done

echo
if [ -n "$failed" ]; then
    echo "FAILED:$failed"
    exit 1
fi
echo "PASSED: $versions"
