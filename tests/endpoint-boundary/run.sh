#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
root="$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)"
mode="${INERTIA_NATIVE_MODE:-pass}"
case "$mode" in pass|endpoint-error-counterexample|lifecycle-counterexample) ;; *) echo 'unknown endpoint test mode' >&2; exit 2;; esac
if [ "$mode" = lifecycle-counterexample ]; then
    case "${INERTIA_LIFECYCLE_CASE:-}" in
        endpoint-queued-move|endpoint-queued-scroll|activity-idle|activity-sleep)
            lifecycle_assertion='Lifecycle transition left an old stream active' ;;
        endpoint-observed-move|endpoint-observed-scroll)
            lifecycle_assertion='Old endpoint inertia reached the replacement target' ;;
        lifecycle-noop)
            lifecycle_assertion='Initial or no-op lifecycle notification cancelled fresh motion' ;;
        *) echo 'select a named lifecycle counterexample case' >&2; exit 2 ;;
    esac
elif [ -n "${INERTIA_LIFECYCLE_CASE:-}" ]; then
    echo 'focused lifecycle selection cannot replace the complete canonical entry' >&2
    exit 2
fi
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
    sha256sum "$work/build/zephyr/zmk.exe" >"$INERTIA_NATIVE_ARTIFACTS/binary.sha256"
fi
for run in 1 2 3; do
    log="$work/run-$run.log"
    if timeout 10 "$work/build/zephyr/zmk.exe" >"$log" 2>&1; then result=0; else result=$?; fi
    cat "$log"
    if [ -n "${INERTIA_NATIVE_ARTIFACTS:-}" ]; then
        cp "$log" "$INERTIA_NATIVE_ARTIFACTS/run-$run.log"
        printf '%s\n' "$result" >"$INERTIA_NATIVE_ARTIFACTS/run-$run.exit"
    fi
    if [ "$mode" = endpoint-error-counterexample ]; then
        test "$result" != 0 && test "$result" != 124
        grep -Fq 'failed endpoint compressed recovery interval' "$log"
        echo "REPRODUCED compressed endpoint recovery repeat $run exit $result"
    elif [ "$mode" = lifecycle-counterexample ]; then
        test "$result" != 0 && test "$result" != 124
        grep -Fq "$lifecycle_assertion" "$log"
        echo "REPRODUCED lifecycle case $INERTIA_LIFECYCLE_CASE repeat $run exit $result"
    else
        test "$result" = 0
        grep -Fq 'inertia endpoint boundary: PASS groups=29' "$log"
        grep -Fq 'inertia lifecycle boundary: PASS groups=11' "$log"
    fi
done
