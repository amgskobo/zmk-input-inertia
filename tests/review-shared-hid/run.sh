#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
set -euo pipefail
workspace="${1:?resolved workspace}"
output="${2:?fresh absolute output}"
case "$output" in /*) ;; *) echo 'output must be absolute' >&2; exit 2;; esac
mode="${3:-pass}"
case "$mode" in pass|counterexample|bypass-manual) ;; *) echo "expected pass, counterexample or bypass-manual" >&2; exit 2;; esac
root="$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)"
if [ -e "$output" ] || [ -L "$output" ]; then echo 'output exists' >&2; exit 2; fi
mkdir -p "$output"
cd "$workspace"
overlays="$root/tests/review-shared-hid/pointer.overlay"
if [ "$mode" = bypass-manual ]; then
    overlays+=";$root/tests/review-shared-hid/bypass-manual.overlay"
fi
west build -p=always -o=-j2 -s "$workspace/zmk/app" -d "$output/build" \
  -b native_sim//zmk_test_mock -- -DCONFIG_ASSERT=y \
  -DZMK_CONFIG="$root/tests/integration/runtime" \
  -DDTC_OVERLAY_FILE="$overlays" \
  -DZMK_EXTRA_MODULES="$root;$root/tests/review-shared-hid" >"$output/build.log" 2>&1
sha256sum "$output/build/zephyr/zmk.exe" >"$output/binary.sha256"
for repeat in 1 2 3; do
    if timeout 10 "$output/build/zephyr/zmk.exe" >"$output/run-$repeat.log" 2>&1; then result=0; else result=$?; fi
    printf '%s\n' "$result" >"$output/run-$repeat.exit"
    if [ "$mode" = counterexample ]; then
        test "$result" != 0 && test "$result" != 124
        grep -Fq 'pending inertia report overwritten by another HID producer' "$output/run-$repeat.log"
        echo "REPRODUCED shared HID overwrite repeat $repeat exit $result"
    elif [ "$mode" = bypass-manual ]; then
        test "$result" != 0 && test "$result" != 124
        grep -Fq 'stale inertia reached output before manual frame' "$output/run-$repeat.log"
        echo "REPRODUCED uncancelled glide repeat $repeat exit $result"
    else
        test "$result" = 0
        grep -Fq 'inertia shared HID regression: PASS' "$output/run-$repeat.log"
        echo "PASS shared HID regression repeat $repeat"
    fi
done
