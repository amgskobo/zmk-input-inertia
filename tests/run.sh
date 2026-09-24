#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT

set -euo pipefail

repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/zmk-input-inertia-test.XXXXXX")"

cleanup() {
    rm -rf "$build_dir"
}
trap cleanup EXIT HUP INT TERM

warnings=(-std=c11 -Wall -Wextra -Werror -pedantic -Wshadow
    -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wconversion -Wsign-conversion)

printf '#include <zmk-input-inertia/inertia_core.h>\n' >"$build_dir/header.c"
cc "${warnings[@]}" -fsyntax-only -I"$repo_root/include" "$build_dir/header.c"

variants=(
    "optimised:-O2"
    "sanitized:-O1 -g -fno-omit-frame-pointer -fsanitize=address,undefined -fno-sanitize-recover=all"
    "coverage:-O0 --coverage"
    "32-bit:-O2 -m32"
)

export ASAN_OPTIONS=detect_leaks=0
export UBSAN_OPTIONS=print_stacktrace=1

for variant in "${variants[@]}"; do
    label="${variant%%:*}"
    read -r -a flags <<<"${variant#*:}"

    cc "${warnings[@]}" "${flags[@]}" -I"$repo_root/include" \
        "$repo_root/src/inertia_core.c" "$repo_root/tests/test_inertia_core.c" \
        -o "$build_dir/test-$label"
    printf 'inertia core (%s): ' "$label"
    "$build_dir/test-$label"
done

coverage=$(cd "$build_dir" && gcov -b -c *coverage*.gcno)
printf '%s\n' "$coverage"
core=$(printf '%s\n' "$coverage" | grep -F -A4 '/src/inertia_core.c')
printf '%s\n' "$core" | grep -Fq 'Lines executed:100.00%'
printf '%s\n' "$core" | grep -Fq 'Taken at least once:100.00%'

python3 "$repo_root/tests/runtime/run.py"
