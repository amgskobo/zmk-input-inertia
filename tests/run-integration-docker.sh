#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail

export MSYS_NO_PATHCONV=1

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
image="${ZMK_TEST_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
workspace=()

if [ -n "${ZMK_TEST_WORKSPACE_VOLUME:-}" ]; then
    workspace=(--volume "$ZMK_TEST_WORKSPACE_VOLUME:/workspace" --env ZMK_TEST_WORKSPACE=/workspace)
fi

docker run --rm \
    --volume "$repo_root:/src:ro" \
    ${workspace[@]+"${workspace[@]}"} \
    "$image" \
    /bin/bash /src/tests/integration/run.sh
