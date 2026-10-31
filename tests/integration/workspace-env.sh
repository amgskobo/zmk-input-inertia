#!/usr/bin/env bash
# SPDX-License-Identifier: MIT
# Keep the selected manifest authoritative when Zephyr itself is a cache symlink.
zmk_test_select_workspace() {
    local workspace projects project
    workspace="$(cd "${1:?resolved workspace required}" && pwd)" || return 2
    if [ "${ZMK_TEST_WORKSPACE_NEGATIVE_CONTROL:-}" = keep-ambient ]; then return 0; fi
    if ! projects="$(cd "$workspace" && west list -f '{abspath}')"; then
        echo 'selected workspace project discovery failed' >&2; return 2
    fi
    local modules=()
    while IFS= read -r project; do
        if [ -f "$project/zephyr/module.yml" ] || [ -f "$project/zephyr/module.yaml" ] ||
           { [ -f "$project/zephyr/CMakeLists.txt" ] && [ -f "$project/zephyr/Kconfig" ]; }; then
            modules+=("$project")
        fi
    done <<< "$projects"
    if [ "${#modules[@]}" -eq 0 ]; then
        echo 'selected workspace has no Zephyr modules' >&2; return 2
    fi
    export ZEPHYR_BASE="$workspace/zephyr"
    export Zephyr_DIR="$workspace/zephyr/share/zephyr-package/cmake"
    export ZEPHYR_MODULES="$(IFS=';'; echo "${modules[*]}")"
}
