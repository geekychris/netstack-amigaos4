#!/usr/bin/env bash
# scripts/import-rump.sh — import NetBSD rump kernel subtrees.
#
# STATUS: placeholder. Does not actually import anything today.
# Real behavior when implemented:
#
#   1. Fetch a NetBSD source tarball at $UPSTREAM_REF.
#   2. Verify sha256 against IMPORT_MANIFEST.txt.
#   3. Extract only the subtrees listed in vendor/netbsd-rump/README.md
#      into vendor/netbsd-rump/.
#   4. Apply local patches from vendor/netbsd-rump/amigaos_patches/.
#   5. Print a summary of file count + total bytes.

set -euo pipefail

UPSTREAM_URL="${UPSTREAM_URL:-https://github.com/NetBSD/src}"
UPSTREAM_REF="${UPSTREAM_REF:-netbsd-10-1-RELEASE}"

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
VENDOR_DIR="${REPO_DIR}/vendor/netbsd-rump"

cat <<EOF
==================================================================
  NetBSD rump kernel import — STUB IMPLEMENTATION

  This script is intentionally not-yet-implemented. Real work:

    1. Extract subtrees listed in vendor/netbsd-rump/README.md
       from ${UPSTREAM_URL} at ref ${UPSTREAM_REF}.
    2. Land them under ${VENDOR_DIR}/.
    3. Apply amigaos_patches/*.
    4. Update the top-level Makefile to include the imported
       .c files in the appropriate phase source lists.

  To actually run the import, edit this script to remove the
  early-exit below and fill in the fetch+extract logic. See the
  FreeBSD src/contrib/ vendor-import pattern for a proven model.
==================================================================
EOF

exit 0
