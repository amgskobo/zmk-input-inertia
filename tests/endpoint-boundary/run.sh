#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
root="$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)"
if [ -n "${INERTIA_NATIVE_ARTIFACTS:-}" ] && { [ -e "$INERTIA_NATIVE_ARTIFACTS" ] || [ -L "$INERTIA_NATIVE_ARTIFACTS" ]; }; then
    echo "output already exists: $INERTIA_NATIVE_ARTIFACTS" >&2
    exit 2
fi
workspace="$(west topdir)"
zmk_root="$(west list zmk -f '{abspath}')"
export ZEPHYR_BASE="$(west list zephyr -f '{abspath}')"
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT HUP INT TERM
if ! (cd "$workspace" && west build -p=always -s "$zmk_root/app" -d "$work/build" \
    -b native_sim//zmk_test_mock -- -DCONFIG_ASSERT=y \
    -DZephyr_DIR="$ZEPHYR_BASE/share/zephyr-package/cmake" \
    -DZMK_CONFIG="$root/tests/integration/runtime" \
    -DDTC_OVERLAY_FILE="$root/tests/endpoint-boundary/test.overlay" \
    -DZMK_EXTRA_MODULES="$root;$root/tests/endpoint-boundary/module") > "$work/build.log" 2>&1; then
    cat "$work/build.log" >&2
    exit 1
fi
if [ -n "${INERTIA_NATIVE_ARTIFACTS:-}" ]; then
    mkdir -p "$INERTIA_NATIVE_ARTIFACTS"
    cp "$work/build.log" "$work/build/CMakeCache.txt" "$INERTIA_NATIVE_ARTIFACTS/"
    cp "$work/build/zephyr/.config" "$INERTIA_NATIVE_ARTIFACTS/config"
    cp "$work/build/zephyr/zephyr.dts" "$work/build/zephyr/zmk.exe" "$INERTIA_NATIVE_ARTIFACTS/"
fi
for run in 1 2 3; do
    if [ -n "${INERTIA_NATIVE_ARTIFACTS:-}" ]; then
        timeout 10 "$work/build/zephyr/zmk.exe" | tee "$INERTIA_NATIVE_ARTIFACTS/run-$run.log"
    else
        timeout 10 "$work/build/zephyr/zmk.exe"
    fi
done
