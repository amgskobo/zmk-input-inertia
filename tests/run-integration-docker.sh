#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
set -euo pipefail
export MSYS_NO_PATHCONV=1
variant="${1:-upstream}"
case "$variant" in upstream|dya) ;; *) echo "expected upstream or dya" >&2; exit 2 ;; esac
repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
image="${ZMK_TEST_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
docker_args=(run --rm --volume "$repo_root:/src:ro")
if [ -n "${ZMK_TEST_OUTPUT:-}" ]; then
    output="$ZMK_TEST_OUTPUT"
    if [[ "$output" != /* ]] || [ -e "$output" ] || [ -L "$output" ]; then
        echo "output must be a fresh absolute path: $output" >&2
        exit 2
    fi
    parent="$(dirname -- "$output")"
    mkdir -p -- "$parent"
    parent="$(CDPATH='' cd -- "$parent" && pwd)"
    docker_args+=(--volume "$parent:/results" --env "ZMK_TEST_OUTPUT=/results/$(basename -- "$output")")
fi
docker "${docker_args[@]}" "$image" /bin/bash /src/tests/integration/run.sh "$variant"
