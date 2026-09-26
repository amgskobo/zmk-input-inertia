#!/usr/bin/env bash
# Copyright (c) 2026 amgskobo
# SPDX-License-Identifier: MIT
#
# Runs inside the ZMK build image; see run-docker.sh. MODULE names the module
# under test and prefixes its executables.

set -uo pipefail

export BSIM_OUT_PATH=/ws/tools/bsim
export BSIM_COMPONENTS_PATH=/ws/tools/bsim/components
export ZEPHYR_BASE=/ws/zephyr

# ----- The workspace, once -----
if [ ! -x "$BSIM_OUT_PATH/bin/bs_2G4_phy_v1" ]; then
    echo "Setting up the BabbleSim workspace in /ws (first run only)"
    mkdir -p /ws/config
    cat >/ws/config/west.yml <<'EOF'
manifest:
  remotes:
    - name: cormoran
      url-base: https://github.com/cormoran
  projects:
    - name: zmk
      remote: cormoran
      revision: main+dya
      import: app/west.yml
  self:
    path: config
EOF
    (
        set -e
        cd /ws
        [ -d .west ] || west init -l config
        west config manifest.group-filter -- +babblesim
        west update --fetch-opt=--filter=tree:0
        make -C tools/bsim everything -j4
    ) >/ws/setup.log 2>&1 || {
        echo "FAILED: workspace setup (see /ws/setup.log in the volume)"
        tail -20 /ws/setup.log
        exit 1
    }
fi
(cd /ws && west zephyr-export >/dev/null 2>&1)

cd /ws/zmk/app

# ZMK's host test central: the "computer" the split central pairs with.
if [ ! -e "$BSIM_OUT_PATH/bin/ble_test_central.exe" ]; then
    west build -p -d /ws/build-bsim/host-central -b nrf52_bsim tests/ble/central \
        >/ws/build-bsim-host.log 2>&1 || {
        echo "FAILED: host central did not build"
        tail -20 /ws/build-bsim-host.log
        exit 1
    }
    cp /ws/build-bsim/host-central/zephyr/zephyr.exe "$BSIM_OUT_PATH/bin/ble_test_central.exe"
fi

tests=("$@")
if [ ${#tests[@]} -eq 0 ]; then
    for dir in /src/tests/bsim/*/; do
        [ -f "$dir/siblings.txt" ] && tests+=("$(basename "$dir")")
    done
fi

status=0
for test in "${tests[@]}"; do
    case_dir="/src/tests/bsim/$test"
    out="/ws/build-bsim/$MODULE-$test"
    exe="$MODULE-$test"
    mkdir -p "$out"

    # The repo itself when it is a module, its script input device when it
    # has one, the modules the caller lists in EXTRA_MODULES, and a test's own
    # module (stand-ins for hardware the simulator lacks, or probes), plus
    # Kconfig and devicetree for one side only.
    modules=""
    if [ -f /src/zephyr/module.yml ]; then
        modules=/src
    fi
    if [ -d /src/tests/bsim/script ]; then
        modules="$modules;/src/tests/bsim/script"
    fi
    if [ -n "${EXTRA_MODULES:-}" ]; then
        modules="$modules;$EXTRA_MODULES"
    fi
    if [ -d "$case_dir/module" ]; then
        modules="$modules;$case_dir/module"
    fi
    modules="${modules#;}"
    central_conf=()
    if [ -f "$case_dir/central.conf" ]; then
        central_conf=("-DEXTRA_CONF_FILE=$case_dir/central.conf")
    fi
    central_overlay=()
    if [ -f "$case_dir/central.overlay" ]; then
        central_overlay=("-DEXTRA_DTC_OVERLAY_FILE=$case_dir/central.overlay")
    fi
    peripheral_conf=()
    if [ -f "$case_dir/peripheral.conf" ]; then
        peripheral_conf=("-DEXTRA_CONF_FILE=$case_dir/peripheral.conf")
    fi

    if ! west build -p -d "$out/peripheral" -b nrf52_bsim//zmk_test_mock -- \
        -DZMK_CONFIG="$case_dir" -DZMK_EXTRA_MODULES="$modules" "${peripheral_conf[@]}" \
        -DEXTRA_DTC_OVERLAY_FILE="$case_dir/peripheral.overlay" >"$out/peripheral.log" 2>&1; then
        echo "FAILED: $test: the peripheral did not build"
        grep -E "error|warning: " "$out/peripheral.log" | head -20
        status=1
        continue
    fi
    if ! west build -p -d "$out/central" -b nrf52_bsim//zmk_test_mock -- \
        -DZMK_CONFIG="$case_dir" -DZMK_EXTRA_MODULES="$modules" \
        -DCONFIG_ZMK_SPLIT_ROLE_CENTRAL=y "${central_conf[@]}" "${central_overlay[@]}" \
        >"$out/central.log" 2>&1; then
        echo "FAILED: $test: the central did not build"
        grep -E "error|warning: " "$out/central.log" | head -20
        status=1
        continue
    fi

    # A case may bring a host of its own, a Zephyr application in host/, in
    # place of ZMK's test central: siblings.txt runs it as @TEST@_host.exe.
    if [ -d "$case_dir/host" ]; then
        if ! west build -p -d "$out/host" -b nrf52_bsim "$case_dir/host" >"$out/host.log" 2>&1; then
            echo "FAILED: $test: the host did not build"
            grep -E "error|warning: " "$out/host.log" | head -20
            status=1
            continue
        fi
        cp "$out/host/zephyr/zephyr.exe" "$BSIM_OUT_PATH/bin/${exe}_host.exe"
    fi

    cp "$out/central/zephyr/zmk.exe" "$BSIM_OUT_PATH/bin/$exe"
    cp "$out/peripheral/zephyr/zmk.exe" "$BSIM_OUT_PATH/bin/${exe}_peripheral.exe"
    (
        cd "$BSIM_OUT_PATH/bin"
        rm -f "$out/output.log"
        # Devices: 0 the split central, 1 the handbrake that keeps simulated
        # time near real time, then the siblings (host central, peripheral).
        siblings=$(wc -l <"$case_dir/siblings.txt")
        "./$exe" -d=0 -s="$exe" >>"$out/output.log" 2>&1 &
        ./bs_device_handbrake -s="$exe" -d=1 -r=10 >/dev/null &
        while IFS= read -r line; do
            line="${line//@TEST@/$exe}"
            $line -s="$exe" >>"$out/output.log" 2>&1 &
        done <"$case_dir/siblings.txt"
        # A case may run longer, and may give the phy arguments of its own: the
        # default modem, Magic, receives every packet whatever else is on the
        # air, so a case with an interferer needs -defmodem=BLE_simple.
        length="$SIM_LENGTH"
        if [ -f "$case_dir/sim_length" ]; then
            length=$(cat "$case_dir/sim_length")
        fi
        phy_args=()
        if [ -f "$case_dir/phy_args" ]; then
            read -r -a phy_args <"$case_dir/phy_args"
        fi
        ./bs_2G4_phy_v1 -s="$exe" -D=$((2 + siblings)) -sim_length="$length" "${phy_args[@]}" \
            >/dev/null 2>&1
        wait
    )
    # Every device appends to the same log, so two lines stamped with the same
    # simulated time can land in either order. Put the log in time order, and
    # the devices in number order within a time; a device's own lines keep
    # theirs.
    LC_ALL=C sort -s -k2,2 -k1,1 "$out/output.log" |
        sed -E -n -f "$case_dir/events.patterns" >"$out/filtered.log"

    if [ -n "$ACCEPT" ]; then
        cp "$out/filtered.log" "$case_dir/snapshot.log"
        echo "ACCEPTED: $test ($(wc -l <"$case_dir/snapshot.log") lines)"
    elif diff -u "$case_dir/snapshot.log" "$out/filtered.log"; then
        echo "PASS: $test"
    else
        echo "FAILED: $test (full log in the volume: $out/output.log)"
        status=1
    fi
done
exit $status
