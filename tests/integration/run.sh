#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail

tests_dir=/src/tests/integration

if [ -n "${ZMK_TEST_WORKSPACE:-}" ]; then
    work_dir="$ZMK_TEST_WORKSPACE"
    mkdir -p "$work_dir"
else
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-input-inertia-integration.XXXXXX")"
    cleanup() {
        rm -rf "$work_dir"
    }
    trap cleanup EXIT HUP INT TERM
fi

workspace="$work_dir/upstream"
mkdir -p "$workspace/config"
cp "$tests_dir/upstream/west.yml" "$workspace/config/west.yml"
cd "$workspace"

if [ ! -d .west ]; then
    west init -l config
fi
west update --narrow --fetch-opt=--depth=1
west zephyr-export

build_dir="$work_dir/build/smoke"
rm -rf "$build_dir"
mkdir -p "$build_dir"

if ! west build -s zmk/app -d "$build_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/smoke" -DZMK_EXTRA_MODULES=/src \
    >"$build_dir.log" 2>&1; then
    echo "FAILED: upstream smoke"
    tail -n 80 "$build_dir.log"
    exit 1
fi
echo "PASS: upstream smoke"

runtime_dir="$work_dir/build/runtime"
rm -rf "$runtime_dir"
mkdir -p "$runtime_dir"

if ! west build -s zmk/app -d "$runtime_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/runtime" \
    -DZMK_EXTRA_MODULES="/src;$tests_dir/module" >"$runtime_dir.log" 2>&1; then
    echo "FAILED: runtime build"
    tail -n 80 "$runtime_dir.log"
    exit 1
fi
echo "PASS: runtime build"

runtime_log="$work_dir/build/runtime.run.log"
if ! timeout 30 "$runtime_dir/zephyr/zmk.exe" >"$runtime_log" 2>&1; then
    echo "FAILED: runtime execution"
    tail -n 80 "$runtime_log"
    exit 1
fi
if ! grep -Fq "inertia runtime tests: PASS" "$runtime_log"; then
    echo "FAILED: runtime tests did not report success"
    tail -n 80 "$runtime_log"
    exit 1
fi
echo "PASS: runtime execution"

bash "$tests_dir/guards/run.sh" "$workspace" "$work_dir/build/guards"

echo "inertia integration: PASS"
