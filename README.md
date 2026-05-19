# ZMK Inertia Input Processor

This module adds a **mouse inertia** effect to the ZMK **input processing pipeline**. After a relative movement input (like a trackpad or trackball) stops, it continues the motion according to a configured decay factor, creating a natural inertial scroll or mouse movement.

Since it operates using only relative coordinate events (`INPUT_EV_REL`), it is compatible with a wide range of standard trackpads and trackballs.

[日本語版ドキュメントはこちら (README_JP.md)](./README_JP.md)

### **✨ Features**

* **Inertial Movement/Scrolling:** Continues and gradually decelerates movement after an input event (mouse movement or scroll) has finished.
* **Q8 Fixed-Point Arithmetic:** Lightweight implementation without floating-point arithmetic minimizes MCU load while achieving smooth decay.
* **Customizable Parameters:** Detailed settings for decay factor, report interval, and start/stop thresholds.

---

## **🛠️ Installation and Setup**

### **1\. Integrate the Module**

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

### **2\. DTS Include**

Add the following line to your keyboard's DTS file.

```dts
#include <zmk-input-inertia/input/processor/input_inertia.dtsi>
```

### **3\. DTS Instance Configuration**

```dts
&zip_inertia {
    // Initiation delay (Wait time after input stops before starting inertia)
    // Recommended: AT LEAST 2x your sensor's polling interval (e.g., 30ms for 15ms sensor).
    trigger-ms = <35>;

    // --- Mouse Movement Settings ---
    move-decay-factor-int = <90>;       // Decay rate (0-100)
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

### **4\. Integration into the Input Processor Pipeline**

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

### **The "2x Rule" (trigger-ms)**

For a smooth operation feel, the `trigger-ms` setting is crucial.

* **Problem:** ZMK processes X and Y axis movements as separate events. Due to processing jitter, the next packet may be delayed by a few milliseconds.
* **Solution:** Set `trigger-ms` to at least **twice your sensor's polling interval**.
  * Example: For a 15ms sensor (e.g. Xiao BLE), **30ms** or **35ms** is recommended.
* **Reason:** This prevents false "stop" detection due to variance in sensor report intervals or processing timing. Providing this buffer ensures that inertia is not accidentally triggered (causing cursor jumpiness) while operation is still ongoing.

---

## **📖 Technical Details**

If velocity information below "1" is discarded during inertia processing, movement stops abruptly and unnaturally.
This module uses `calculate_decayed_movement_fixed` (Q8 fixed-point arithmetic) to solve this problem through the following 4-step process:

1. **Expansion (Integration):**
    Input velocity is expanded to Q8 format (x256), and the "Remainder" (sub-pixel value) from the previous calculation is added.
    `Ideal Velocity (Q8) = (Input Velocity << 8) + Remainder`
2. **Decay:**
    The decay factor is applied to this entire "Ideal Velocity". This ensures that not only the integer part but also the accumulated remainder is accurately decayed.
3. **Rounding:**
    The decayed value is **rounded** to determine the integer value for the HID report. Rounding (adding 0.5 before shifting) prevents bias near zero compared to simple truncation.
4. **Update Remainder:**
    The difference `Decayed Value (Q8) - Output Value (Q8)` is calculated to strictly extract the "Quantization Error" caused by integer output. This value is saved as the next "Remainder", ensuring zero information loss.

### ⚡ Why is it fast?

Many embedded MCUs used with ZMK have limited hardware support for floating-point arithmetic.
This module performs all calculations using only **integer addition, multiplication, and bit shifting**, completing processing in significantly fewer CPU cycles compared to floating-point operations. This minimizes input latency even when using high-polling-rate sensors.
