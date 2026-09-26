# ZMK Inertia Input Processor

[![Test](https://github.com/amgskobo/zmk-input-inertia/actions/workflows/test.yml/badge.svg)](https://github.com/amgskobo/zmk-input-inertia/actions/workflows/test.yml)

[日本語](README_JA.md)

This module adds a **mouse inertia** effect to the ZMK **input processing pipeline**. After a relative movement input (like a trackpad or trackball) stops, it continues the motion according to a configured decay factor, creating a natural inertial scroll or mouse movement.

Since it operates using only relative coordinate events (`INPUT_EV_REL`), it is compatible with a wide range of standard trackpads and trackballs.

## ✨ Features

* **Inertial Movement/Scrolling:** Continues and gradually decelerates movement after an input event (mouse movement or scroll) has finished.
* **Q8 Fixed-Point Arithmetic:** Lightweight implementation without floating-point arithmetic minimizes MCU load while achieving smooth decay.
* **Customizable Parameters:** Detailed settings for decay factor, report interval, and start/stop thresholds.

---

## 🛠️ Installation and Setup

### 1. Integrate the Module

Add this module to your project's `config/west.yml` file.

```yaml
manifest:
  remotes:
    - name: amgskobo
      url-base: https://github.com/amgskobo
  projects:
    - name: zmk-input-inertia
      remote: amgskobo
      revision: main
```

### 2. DTS Include

Add the following line to your keyboard's DTS file.

```dts
#include <zmk-input-inertia/input/processor/input_inertia.dtsi>
```

### 3. DTS Instance Configuration

```dts
&zip_inertia {
    // Initiation delay (Wait time after input stops before starting inertia)
    // Recommended: AT LEAST 2x your sensor's polling interval (e.g., 30ms for 15ms sensor).
    trigger-ms = <35>;

    // --- Mouse Movement Settings ---
    move-decay-factor-int = <90>;       // Velocity retained per update (0-99%)
    move-report-interval-ms = <35>;     // Report interval (ms)
    move-threshold-start = <15>;        // Start threshold (pix/report)
    move-threshold-stop = <1>;          // Stop threshold (pix/report)

    // --- Scrolling Settings ---
    scroll-decay-factor-int = <85>;    // Scroll decay rate
    scroll-report-interval-ms = <65>;  // Scroll report interval (ms)
    scroll-threshold-start = <2>;      // Start threshold (pix/report)
    scroll-threshold-stop = <0>;       // Stop threshold (pix/report)

    // Optional: stop/suppress scroll inertia while Ctrl is pressed.
    cancel-scroll-inertia-on-ctrl;
};
```

### 4. Integration into the Input Processor Pipeline

Add `&zip_inertia` to the **end** of your `input-processors` list.

This inertia processor sends synthesized inertia events directly to the HID endpoint, bypassing any subsequent input processors. Therefore, it is critical to always place it at the **end** of the input processors pipeline so it operates on the final processed values (e.g., after scaling or scroll mapping) to avoid malfunctions.

```dts
&trackball_listener {
    // 1. Normal Mouse Movement (Default)
    input-processors = <&zip_xy_scaler 1 1>,
                       <&zip_inertia>; // Shared Node

    // 2. Scroll Mode (e.g., active on layer 1)
    scroll {
        layers = <1>;
        input-processors = <&zip_xy_to_scroll_mapper>, // Transform move to scroll
                           <&zip_inertia>; // Shared Node (Important: Reference the same node)
    };
};
```

> [!IMPORTANT]
> **Sharing with Scroll and Mouse Move**
> To instantly stop inertia when moving the mouse cursor during inertial scrolling (or vice versa), **Scroll and Mouse Move must reference the same Device Tree Node (`&zip_inertia`).** This allows them to share state, detect different operations, and perform a natural stop.

---

## 🚀 Optimization Guide

### The "2x Rule" (trigger-ms)

For a smooth operation feel, the `trigger-ms` setting is crucial.

* **Problem:** ZMK processes X and Y axis movements as separate events. Due to processing jitter, the next packet may be delayed by a few milliseconds.
* **Solution:** Set `trigger-ms` to at least **twice your sensor's polling interval**.
  * Example: For a sensor that reports every 15ms, **30ms** or **35ms** is recommended.
* **Reason:** This prevents false "stop" detection due to variance in sensor report intervals or processing timing. Providing this buffer ensures that inertia is not accidentally triggered (causing cursor jumpiness) while operation is still ongoing.

---

## 📖 Technical Details

If velocity information below "1" is discarded during inertia processing, movement stops abruptly and unnaturally.
This module uses Q8 fixed-point arithmetic to preserve sub-count motion between updates:

1. **Expansion (Integration):**
    Input velocity is expanded to Q8 format (x256), and the "Remainder" (sub-pixel value) from the previous calculation is added.
    `Ideal Velocity (Q8) = Input Velocity * 256 + Remainder`
2. **Decay:**
    The decay factor is applied to this entire "Ideal Velocity". This ensures that not only the integer part but also the accumulated remainder is accurately decayed.
3. **Rounding:**
    The decayed value is rounded to the nearest integer, with ties away from zero. Positive and negative motion therefore follow symmetric paths.
4. **Update Remainder:**
    The difference `Decayed Value (Q8) - Output Value (Q8)` becomes the next remainder. This carries Q8 quantization error forward instead of discarding it at every update.

### ⚡ Why is it fast?

Many embedded MCUs used with ZMK have limited hardware support for floating-point arithmetic.
This module performs the hot-path calculation with 32-bit integer arithmetic. The percentage-to-Q8 conversion is resolved at build time, and one work item is scheduled per completed input frame rather than once per axis.

## Configuration Reference

| Property | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| `trigger-ms` | int | 35 | Delay after manual input stops before inertia starts. Range: 1-65535 ms. Set it to at least twice the sensor's polling interval. |
| `move-decay-factor-int` | int | 90 | Velocity retained per report interval. Range: 0-99%. Higher is slipperier. |
| `move-report-interval-ms` | int | 35 | Interval between inertia movement reports. Range: 1-65535 ms. |
| `move-threshold-start` | int | 15 | Minimum absolute delta from the last manual frame needed to start inertia. Range: 1-32767. |
| `move-threshold-stop` | int | 1 | Inertia stops when both axes are at or below this value. Range: 0 to one less than `move-threshold-start`. |
| `scroll-decay-factor-int` | int | 85 | Velocity retained per report interval while scrolling. Range: 0-99%. |
| `scroll-report-interval-ms` | int | 65 | Interval between scroll inertia updates. Range: 1-65535 ms. |
| `scroll-threshold-start` | int | 2 | Minimum absolute delta needed to start scroll inertia. Range: 1-32767. |
| `scroll-threshold-stop` | int | 0 | Scroll inertia stops when both axes are at or below this value. Range: 0 to one less than `scroll-threshold-start`. |
| `cancel-scroll-inertia-on-ctrl` | bool | false | Stop active scroll inertia and suppress new scroll inertia while Ctrl is held, so Ctrl+wheel does not zoom the host by accident. |

### Decay factor range

The decay factors are percentages and are checked at build time:

| Value | Effect |
| :--- | :--- |
| `0` | no inertia report is emitted when the trigger fires |
| `1`-`99` | velocity decays, faster at lower values |
| `100` or more | rejected at build time because inertia would not converge |

Invalid intervals, threshold ranges, and threshold ordering are also rejected at build time.

## Concurrency and multiple listeners

X and Y events are accumulated until the event marked `sync` closes the input frame. An axis omitted by the device is treated as zero for that frame. Each ZMK input listener has independent frame and velocity history, so two devices cannot combine their axes accidentally. An event whose listener index is outside the listeners the build defines has no history of its own: it passes through unchanged instead of joining the first listener's.

Manual pointer or scroll input cancels conflicting pending and active inertia across listeners. Each cancellation advances an atomic generation counter. A delayed callback compares the generation before emitting its report and discards stale output if manual input changed the state while it was running.

Decay callbacks and their HID reports run on ZMK's shared low-priority work
queue, not Zephyr's system work queue. Continuous inertia therefore cannot
delay system work such as Bluetooth, split and watchdog housekeeping.

## Testing

Run the dependency-free arithmetic and frame tests:

```sh
bash ./tests/run-docker.sh
```

Run the upstream ZMK build and devicetree guard suite:

```sh
bash ./tests/run-integration-docker.sh
```

The core suite runs optimized, ASan/UBSan, coverage, and 32-bit variants. It
then lifts every function of the driver into the stubbed harnesses in
`tests/runtime/` and runs them optimized, under ASan/UBSan, and with coverage:
nine stream and frame helpers, and the seven that finish frames, decay and
emit a glide, cancel scrolling on Ctrl, and handle events. The emit path is
tested with a settings or input change landing between the decision and the
report. CI requires 100% line and branch coverage of `inertia_core.c` and of
each lifted function; devicetree instantiation is left to the integration
suite. The integration suite builds against
current upstream ZMK and verifies that unsafe devicetree values fail for the
expected reason.

## License

This project is licensed under the [MIT License](./LICENSE).
