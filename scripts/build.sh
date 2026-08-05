#!/usr/bin/env bash
# scripts/build.sh — cross-compile via the walkero docker toolchain.
#
# Same pattern as sibling projects: run the docker container with
# the repo mounted at /work and invoke `make`.
#
# Usage:
#   ./scripts/build.sh              # make all
#   ./scripts/build.sh phase1       # single phase
#   ./scripts/build.sh clean

set -euo pipefail

REPO_DIR="$(cd "$(dirname "$0")/.." && pwd)"
IMAGE="walkero/amigagccondocker:os4-gcc11-arm64"

if ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
    echo "==> Pulling toolchain image ${IMAGE} (once)..."
    docker pull "$IMAGE"
fi

TARGET="${1:-all}"

echo "==> make ${TARGET}"
docker run --rm \
    -v "${REPO_DIR}:/work" \
    -w /work \
    "$IMAGE" \
    make "$TARGET"
