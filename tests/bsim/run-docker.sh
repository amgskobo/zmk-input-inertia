#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
#
# Split tests over BabbleSim, Zephyr's simulated 2.4 GHz radio. Each test builds
# a split central and its peripheral for nrf52_bsim with this module, runs them
# next to ZMK's host test central, and compares their filtered log with the
# test's snapshot.log. The peripheral's mock input crosses the real split link,
# and the host central prints the HID reports the central sends it.
#
# The first run sets up a west workspace in the Docker volume zmk-bsim-ws (the
# DYA ZMK fork with its BabbleSim group, and BabbleSim itself built). Later runs
# reuse it.
#
# usage: tests/bsim/run-docker.sh [--accept] [test ...]
#   --accept  write each test's output as its new snapshot.log

set -euo pipefail

export MSYS_NO_PATHCONV=1

module=inertia
repo_root="$(CDPATH='' cd -- "$(dirname -- "$0")/../.." && pwd)"
image="${ZMK_TEST_IMAGE:-zmkfirmware/zmk-build-arm:stable}"
volume="${ZMK_BSIM_VOLUME:-zmk-bsim-ws}"

accept=""
if [ "${1:-}" = "--accept" ]; then
    accept=1
    shift
fi
mode=ro
if [ -n "$accept" ]; then
    mode=rw
fi

docker run --rm \
    --volume "$volume:/ws" \
    --volume "$repo_root:/src:$mode" \
    --env MODULE="$module" \
    --env ACCEPT="$accept" \
    --env SIM_LENGTH="${SIM_LENGTH:-20e6}" \
    "$image" \
    /bin/bash /src/tests/bsim/run.sh "$@"
