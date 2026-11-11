#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
workspace="${1:?resolved workspace}"
output="${2:?fresh absolute output}"
root="$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)"
case "$output" in /*) ;; *) echo 'output must be absolute' >&2; exit 2;; esac
if [ -e "$output" ] || [ -L "$output" ]; then echo 'output exists' >&2; exit 2; fi
mkdir -p "$output"
cd "$workspace"
case_names=(auto limited wide fallback sparse-auto disabled-device reject-base reject-child reject-sparse reject-negative reject-over)
if [ -n "${INERTIA_SOURCE_LIMIT_GUARD:-}" ]; then
    case "$INERTIA_SOURCE_LIMIT_GUARD" in
        base|child|sparse) case_names=("reject-$INERTIA_SOURCE_LIMIT_GUARD");;
        device) case_names=(disabled-device);;
        *) echo 'expected base, child, sparse or device guard control' >&2; exit 2;;
    esac
fi
for case_name in "${case_names[@]}"; do
    bound=0
    overlay=valid
    expected=''
    case "$case_name" in
        limited) bound=2;;
        wide) bound=256;;
        fallback) overlay=no-listeners;;
        sparse-auto) overlay=sparse;;
        disabled-device) bound=2; overlay=disabled-device;;
        reject-base) bound=1; overlay=base; expected='Inertia source limit excludes a referenced listener index';;
        reject-child) bound=1; overlay=child; expected='Inertia source limit excludes a referenced listener index';;
        reject-sparse) bound=2; overlay=sparse; expected='Inertia source limit excludes a referenced listener index';;
        reject-negative) bound=-1; expected='outside the active range';;
        reject-over) bound=257; expected='outside the active range';;
    esac
    conf="$output/$case_name.conf"
    printf 'CONFIG_ZMK_INPUT_INERTIA_MAX_SOURCES=%s\n' "$bound" >"$conf"
    build="$output/$case_name"
    if west build -s "$workspace/zmk/app" -d "$build" -b native_sim//zmk_test_mock -- \
        -DCONFIG_ASSERT=y -DZMK_CONFIG="$root/tests/integration/runtime" \
        -DDTC_OVERLAY_FILE="$root/tests/source-limits/$overlay.overlay" \
        -DEXTRA_CONF_FILE="$conf" \
        -DZMK_EXTRA_MODULES="$root;$root/tests/source-limits/module" >"$output/$case_name.log" 2>&1; then
        result=0
    else result=$?; fi
    printf '%s\n' "$result" >"$output/$case_name.exit"
    if [ -n "$expected" ]; then
        if [ "$result" = 0 ] || ! grep -Fq "$expected" "$output/$case_name.log"; then
            echo "FAILED: $case_name requires named rejection: $expected"
            tail -n 60 "$output/$case_name.log"
            exit 1
        fi
        echo "PASS source limit $case_name: $expected"
        continue
    fi
    if [ "$result" != 0 ]; then
        echo "FAILED: $case_name must build with its declared callback sources"
        tail -n 60 "$output/$case_name.log"
        exit 1
    fi
    grep -Fqx "CONFIG_ZMK_INPUT_INERTIA_MAX_SOURCES=$bound" "$build/zephyr/.config"
    sha256sum "$build/zephyr/zmk.exe" >"$output/$case_name.binary.sha256"
    for repeat in 1 2 3; do
        log="$output/$case_name.run-$repeat.log"
        if timeout 10 "$build/zephyr/zmk.exe" >"$log" 2>&1; then result=0; else result=$?; fi
        printf '%s\n' "$result" >"$output/$case_name.run-$repeat.exit"
        python3 "$root/tests/integration/native_log.py" "$log"
        test "$result" = 0
        grep -Fq 'inertia source limit: PASS' "$log"
    done
    echo "PASS source limit $case_name: three same-binary runs"
done
if [ -n "${INERTIA_SOURCE_LIMIT_GUARD:-}" ]; then
    echo 'inertia source limit guard control: PASS (not the complete source-limit matrix)'
    exit 0
fi
INERTIA_NATIVE_EXTRA_CONF="$root/tests/source-limits/limited.conf" \
    INERTIA_NATIVE_ARTIFACTS="$output/endpoint-limited" bash "$root/tests/endpoint-boundary/run.sh"
INERTIA_NATIVE_EXTRA_CONF="$root/tests/source-limits/limited.conf" \
    bash "$root/tests/review-shared-hid/run.sh" "$workspace" "$output/shared-hid-limited"
echo 'inertia source limits: PASS profiles=6 guards=5 limited-endpoint=40 limited-HID=7'
