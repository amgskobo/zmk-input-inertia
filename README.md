# ZMK Inertia Input Processor

This module adds a **mouse inertia** effect to the ZMK **input processing pipeline**. After a relative movement input (like a trackpad or trackball) stops, it continues the motion according to a configured decay factor, creating a natural inertial scroll or mouse movement.

[日本語版ドキュメントはこちら (README_JP.md)](./README_JP.md)

### **✨ Features**

* **Inertial Movement/Scrolling:** Continues and gradually decelerates movement after an input event (mouse movement or scroll) has finished.  
* **Q8 Fixed-Point Arithmetic:** Uses Q8 fixed-point arithmetic for inertia decay calculation, balancing **precision** and **performance**.  
* **Smart Triggering (`trigger-ms`):** Precisely detects the end of manual input to prevent jitter-induced accidental triggers.
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
    move-decay-factor-int = <90>;
    move-report-interval-ms = <35>;
    move-threshold-start = <15>;
    move-threshold-stop = <1>;

    // --- Scrolling Settings ---
    scroll-decay-factor-int = <85>;
    scroll-report-interval-ms = <65>;
    scroll-threshold-start = <2>;
    scroll-threshold-stop = <0>;
};
```

### **4\. Integration into the Input Processor Pipeline**

Add `&zip_inertia` to the **end** of your `input-processors` list.

```dts
input-processors = <&zip_xy_scaler 1 1>,
                   <&zip_inertia>;
```

---

## **⚙️ Property Reference**

| Property Name | Type | Default | Description |
| :--- | :--- | :--- | :--- |
| **trigger-ms** | int | 35 | **Trigger Delay**: Wait time after input stops before inertia starts. |
| move-decay-factor-int | int | 90 | **Decay**: Velocity retention % (0-100) per report interval. |
| move-report-interval-ms | int | 35 | **Cycle**: Interval (ms) for sending inertia events. |
| move-threshold-start | int | 15 | **Start Threshold**: Min velocity to initiate inertia. |
| move-threshold-stop | int | 1 | **Stop Threshold**: Velocity floor to stop inertia. |
| scroll-* | - | - | Same as above, applied to scrolling. |

> [!TIP]
> **The 2x Polling Rule**: Set `trigger-ms` to at least **twice your sensor's polling interval** to prevent stuttering during manual movement.

---

## **📖 Technical Details**

The inertia decay calculation uses Q8 fixed-point arithmetic. This allows sub-integer movements to accumulate as "remainders" and be output once they cross integer boundaries, resulting in smooth deceleration even at very low speeds.
