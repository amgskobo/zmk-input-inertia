#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail

workspace="$1"
build_root="$2"
guards_dir=/src/tests/integration/guards

mkdir -p "$build_root"

for keymap in "$guards_dir"/*/native_sim.keymap; do
    case_dir="$(dirname "$keymap")"
    name="$(basename "$case_dir")"
    build_dir="$build_root/$name"
    log="$build_root/$name.log"

    rm -rf "$build_dir"
    if (cd "$workspace" && west build -s zmk/app -d "$build_dir" \
        -b native_sim//zmk_test_mock -- -DZMK_CONFIG="$case_dir" \
        -DZMK_EXTRA_MODULES=/src) >"$log" 2>&1; then
        echo "FAILED: $name unexpectedly built"
        exit 1
    fi

    while IFS= read -r expected; do
        if ! grep -Fq "$expected" "$log"; then
            echo "FAILED: $name did not fail for the expected reason: $expected"
            tail -n 60 "$log"
            exit 1
        fi
    done <"$case_dir/expected-errors.txt"

    echo "PASS: $name rejected"
done
