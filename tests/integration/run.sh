#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail

tests_dir=/src/tests/integration

variant="${1:-upstream}"
case "$variant" in upstream|dya) ;; *) echo "expected upstream or dya" >&2; exit 2 ;; esac
# Refuse existing evidence before dependency tools or workspace preparation.
if [ -n "${ZMK_TEST_OUTPUT:-}" ]; then
    work_dir="$ZMK_TEST_OUTPUT"
    if [[ "$work_dir" != /* ]] || [ -e "$work_dir" ] || [ -L "$work_dir" ]; then
        echo "output must be a fresh absolute path: $work_dir" >&2
        exit 2
    fi
elif [ -n "${ZMK_TEST_OFFLINE_WORKSPACE:-}" ]; then
    echo "set ZMK_TEST_OUTPUT to a fresh absolute path for offline integration" >&2
    exit 2
else
    work_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-input-inertia-integration.XXXXXX")"
    trap 'rm -rf "$work_dir"' EXIT HUP INT TERM
fi

if [ -n "${ZMK_TEST_OFFLINE_WORKSPACE:-}" ]; then
    workspace="$(python3 "$tests_dir/offline.py" "$ZMK_TEST_OFFLINE_WORKSPACE" "$variant" "$tests_dir/$variant/west.yml" "$work_dir")"
else
    if [ -n "${ZMK_TEST_OUTPUT:-}" ]; then
        mkdir -p -- "$(dirname -- "$work_dir")"
        mkdir -- "$work_dir"
    fi
    workspace="$work_dir/workspace"
    mkdir -p "$workspace/config"
    cp "$tests_dir/$variant/west.yml" "$workspace/config/west.yml"
    cd "$workspace"
    west init -l config
    west update --narrow --fetch-opt=--depth=1
fi
cd "$workspace"
west zephyr-export
if [ -n "${ZMK_TEST_OFFLINE_WORKSPACE:-}" ]; then
    # Unused imported projects may be intentionally absent from an offline cache.
    # dependencies.json records the checked build pins; resolve imports without
    # requiring unrelated projects merely to record the manifest.
    west manifest --resolve >"$work_dir/resolved-manifest.yml"
else
    # west update skips disabled groups (including unused BabbleSim projects).
    # Freeze every selected project without requiring inactive uncloned ones.
    west manifest --freeze --active-only >"$work_dir/resolved-manifest.yml"
fi
source /src/tests/integration/workspace-env.sh
zmk_test_select_workspace "$workspace"

cmake_args=()
if [ "$variant" = dya ]; then cmake_args+=("-DEXTRA_CONF_FILE=$tests_dir/custom-settings.conf"); fi
build_dir="$work_dir/build/smoke"
mkdir -p "$build_dir"

if ! west build -s zmk/app -d "$build_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/smoke" -DZMK_EXTRA_MODULES=/src "${cmake_args[@]}" \
    >"$build_dir.log" 2>&1; then
    echo "FAILED: $variant smoke"
    tail -n 80 "$build_dir.log"
    exit 1
fi
echo "PASS: $variant smoke"

runtime_dir="$work_dir/build/runtime"
mkdir -p "$runtime_dir"

if ! west build -s zmk/app -d "$runtime_dir" -b native_sim//zmk_test_mock -- \
    -DCONFIG_ASSERT=y -DZMK_CONFIG="$tests_dir/runtime" \
    -DZMK_EXTRA_MODULES="/src;$tests_dir/module" "${cmake_args[@]}" >"$runtime_dir.log" 2>&1; then
    echo "FAILED: runtime build"
    tail -n 80 "$runtime_dir.log"
    exit 1
fi
echo "PASS: runtime build"

if [ "$variant" = dya ]; then
    grep -q '^CONFIG_ZMK_INPUT_INERTIA_CUSTOM_SETTINGS=y' "$runtime_dir/zephyr/.config"
else
    ! grep -q '^CONFIG_ZMK_INPUT_INERTIA_CUSTOM_SETTINGS=y' "$runtime_dir/zephyr/.config"
fi
for repeat in 1 2 3; do
    flash=()
    if [ "$variant" = dya ]; then flash=("--flash=$runtime_dir/flash-$repeat.bin"); fi
    runtime_log="$work_dir/build/runtime.run-$repeat.log"
    timeout 30 "$runtime_dir/zephyr/zmk.exe" "${flash[@]}" >"$runtime_log" 2>&1
    grep -Fq 'inertia runtime tests: PASS' "$runtime_log"
    if [ "$variant" = dya ]; then
        timeout 30 "$runtime_dir/zephyr/zmk.exe" "${flash[@]}" >"$runtime_dir/reload-$repeat.log" 2>&1
        grep -Fq 'persisted Inertia value reloads' "$runtime_dir/reload-$repeat.log"
    fi
done
if [ "$variant" = dya ]; then
    python3 "$tests_dir/check-settings-logs.py" "$work_dir/build/runtime.run-{i}.log" "$runtime_dir/reload-{i}.log"
fi
echo "PASS: runtime execution three times; DYA includes NVS reload pairs"

INERTIA_NATIVE_ARTIFACTS="$work_dir/endpoint-boundary" bash /src/tests/endpoint-boundary/run.sh

bash /src/tests/review-shared-hid/run.sh "$workspace" "$work_dir/shared-hid"

bash "$tests_dir/guards/run.sh" "$workspace" "$work_dir/build/guards"

echo "inertia integration: PASS"
