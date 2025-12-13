# ZMK Inertia Input Processor
This module adds a **mouse inertia** effect to the ZMK **input processing pipeline**. After a relative movement input (like a trackpad or trackball) stops, it continues the motion according to a configured decay factor, creating a natural inertial scroll or mouse movement.

### **✨ Features**

* **Inertial Movement/Scrolling:** Continues and gradually decelerates movement after an input event (mouse movement or scroll) has finished.  
* **Q8 Fixed-Point Arithmetic:** Uses Q8 fixed-point arithmetic for inertia decay calculation, balancing **precision** and **performance**.  
* **Mouse/Scroll Mode:** Allows selection of whether to send Mouse Movement (INPUT\_REL\_X/Y) or Scroll (INPUT\_REL\_HWHEEL/WHEEL) HID reports.  
*   **Customizable Parameters:** Detailed settings for decay factor, report interval, and start/stop thresholds.

### **✅ Zephyr 4.1 Compatibility**

As of ZMK's migration to **Zephyr 4.1**, this module remains fully compatible.
*   **Input Subsystem:** This module utilizes the standard Zephyr Input Subsystem (`input_event`, `input_processor`), which is the modern standard ZMK is migrating *towards* (replacing the older `kscan` API).
*   **HWMv2:** The Hardware Model V2 changes primarily affect board definitions. As a driver module, this codebase is unaffected and works seamlessly with HWMv2-based setups.


## **🛠️ Installation and Setup**

### **1\. Integrate the Module**

Add this module to your project's `config/west.yml` file.
```
manifest:
  remotes:
    ...
    # START #####
    - name: amgskobo
      url-base: https://github.com/amgskobo
    # END #######
  projects:
    ...
    # START #####
    - name: zmk-input-inertia
      remote: amgskobo
      revision: main
    # END #######
```

### **2\. DTS Include**

Add the following line to your keyboard's DTS file (e.g., boards/arm/my\_keyboard/my\_keyboard.dts) to include the module definitions.
```
#include <input_inertia.dtsi>
```
### **3\. DTS Instance Configuration**

Adjust the inertia processor instance settings as needed.

#### **For Mouse Movement (Default)**
```
&zip_inertia {  
    // Decay factor (0-100). Lower number stops motion faster.  
    // Example: 90 (Slightly slow decay)  
    decay-factor-int = <90>;         
    // Interval for sending HID reports (milliseconds). Lower number is smoother but increases CPU load.  
    // !! NOTE: This must be set greater than or equal to the pointing device's polling rate (e.g., 10ms).  
    // Example: 35ms  
    report-interval-ms = <35>;       
    // Minimum velocity threshold required to start inertial movement.  
    // threshold-start = <15>;       // Default  
    // Velocity threshold to stop inertial movement.  
    // threshold-stop = <1>;         // Default  
};
```
#### **For Scrolling (scroll-mode)**

This instance is used to achieve inertial scrolling.
```
&zip_inertia_scroll {  
    // Enable scroll mode  
    scroll-mode;  
      
    // Faster decay (e.g., 85)  
    decay-factor-int = <85>;         
    // Report at a longer interval (e.g., 65ms)  
    report-interval-ms = <65>;  
    // You might want to start/stop inertia from lower speeds for scrolling  
    threshold-start = <2>;  
    threshold-stop = <1>;  
};
```
### **4\. Integration into the Input Processor Pipeline**

Add the configured inertia processor instance to the input-processors list within your zmk,input-listener node.

⚠️ **IMPORTANT:** This inertia processor **does not forward** processed mouse or scroll events to the next processor. Therefore, it **must** be placed at the **end** of the input processor pipeline.

#### **Example: Applying Inertia to Mouse Movement**
```
/ {  
    trackpad_input_listener: trackpad_input_listener {  
        compatible = "zmk,input-listener";  
        // ... (Other configurations)  
        input-processors = <&zip_xy_scaler 1 1>, // E.g., a scaler  
                           <&zip_inertia>;       // Inertia processor MUST be placed last  
        // ...  
    };  
};
```
#### **Example: Applying Inertia to Scrolling**
```
/ {  
    trackpad_input_listener: trackpad_input_listener {  
        // ...  
        scroller {  
            // ... (Other configurations)  
            input-processors = <&zip_xy_transform (INPUT_TRANSFORM_Y_INVERT)>,  
                               <&zip_xy_scaler 1 10>,
                               <&zip_xy_to_scroll_mapper>,
                               <&zip_inertia_scroll>; // Inertia processor MUST be placed last  
        };  
    };  
};
```
## **⚙️ DTS Property Reference**

| Property Name      | Type    | Default Value | Description                                                                                                                                  |
|:-------------------|:--------|:--------------|:---------------------------------------------------------------------------------------------------------------------------------------------|
| decay-factor-int   | uint16_t  | 90            | Inertia decay factor (0-100). Closer to 100 means slower deceleration.                                                                       |
| report-interval-ms | uint16_t  | 35            | HID report interval (in milliseconds) during inertial movement. Must be set **greater than or equal to** the pointing device's polling rate. |
| threshold-start    | uint16_t  | 15            | Minimum input velocity threshold required to start inertial movement.                                                                        |
| threshold-stop     | uint16_t  | 1             | Velocity threshold to stop inertial movement (stops when speed is $\\leq$ this value).                                                       |
| scroll-mode        | boolean | false         | If true, sends scroll HID reports instead of mouse movement.                                                                                 |

## **📖 Technical Details**

### **📐 Q8 Fixed-Point Decay Function**

The inertia decay calculation uses Q8 fixed-point arithmetic, where:

* **Decay Calculation Mechanism:**  
  1. The current integer velocity (in\_dx, in\_dy) is converted to Q8 format, and the previous **remainder** (\*rem\_x, \*rem\_y) is added to get the "true" Q8 velocity.  
  2. This Q8 velocity is multiplied by the configured decay factor (decay\_factor\_q8) and then divided by $2^8$ (right-shift by 8\) to apply the decay.  
  3. From the decayed Q8 value, the integer part is extracted (with rounding) as the output velocity (\*out\_dx, \*out\_dy), and the fractional part is saved as the **remainder** for the next tick.

This approach allows **sub-integer movements** from the decay process to accumulate and eventually be output as an integer value, resulting in a **smoother and more accurate deceleration**.


# ZMK Inertia Input Processor (JP)
このモジュールは、ZMKの**入力処理パイプライン**に**マウスの慣性（Inertia）効果を追加します。トラックパッドやトラックボールなどの相対移動入力が終了した後、設定された減衰率に従って動きを継続**させ、自然な慣性スクロールやマウス移動を実現します。

### **✨ 機能概要**

* **慣性移動/スクロール:** 入力イベント（マウス移動またはスクロール）が停止した後、動きを徐々に減速させながら継続します。  
* **Q8固定小数点演算:** 慣性減衰の計算にQ8固定小数点演算を採用し、**精度**と**パフォーマンス**のバランスを取っています。  
* **マウス/スクロールモード:** マウス移動（INPUT\_REL\_X/Y）またはスクロール（INPUT\_REL\_HWHEEL/WHEEL）のいずれのHIDレポートを送信するかを選択できます。  
* **カスタマイズ可能なパラメーター:** 減衰率、レポート間隔、開始・停止のしきい値などを詳細に設定できます。

### **✅ Zephyr 4.1 互換性**

ZMKの**Zephyr 4.1**への移行において、このモジュールは完全な互換性を維持しています。
*   **入力サブシステム:** 本モジュールは、ZMKが移行を進めている標準のZephyr入力サブシステム（`input_event`, `input_processor`）を使用しています（古い`kscan` APIの代替）。
*   **HWMv2:** Hardware Model V2の変更は主にボード定義に影響します。ドライバーモジュールである本コードベースは影響を受けず、HWMv2ベースの設定でシームレスに動作します。


## **🛠️ インストールと設定**
### **1\. Integrate the Module**
モジュールの組み込みプロジェクトの`config/west.yml`ファイルに、このモジュールを追加します。
```
manifest:
  remotes:
    ...
    # START #####
    - name: amgskobo
      url-base: https://github.com/amgskobo
    # END #######
  projects:
    ...
    # START #####
    - name: zmk-input-inertia
      remote: amgskobo
      revision: main
    # END #######
```
### **2\. DTS Include**
DTSインクルードキーボードのDTSファイル（例: boards/arm/my_keyboard/my_keyboard.dts）に以下の行を追加し、モジュール定義をインクルードします。
```
#include <input_inertia.dtsi>
```
### **3\. DTS Instance Configuration**
DTSインスタンス設定必要に応じて、慣性プロセッサのインスタンス設定を調整します。
#### **Example: Applying Inertia to Mouse Movement**
```
&zip_inertia {
    // 減衰係数 (0-100)。数値が小さいほど速く停止します。
    // 例: 90 (ややゆっくり減速)
    decay-factor-int = <90>;       
    // HIDレポートを送信する間隔 (ミリ秒)。数値が小さいほどスムーズですが、CPU負荷が増します。
    // !! 注意: ポインティングデバイスのポーリングレート (例: 10ms) 以上に設定してください。
    // 例: 35ms
    report-interval-ms = <35>;     
    // 慣性移動を開始するための最低速度しきい値。
    // threshold-start = <15>;       // デフォルト
    // 慣性移動を停止する速度しきい値。
    // threshold-stop = <1>;         // デフォルト
};
```
#### **For Scrolling (scroll-mode)**
このインスタンスは、慣性スクロールを実現するために使用されます。
```
&zip_inertia_scroll {
    // スクロールモードを有効化
    scroll-mode;
    
    // より速い減衰 (例: 85)
    decay-factor-int = <85>;       
    // より長い間隔でレポート送信 (例: 65ms)
    report-interval-ms = <65>;
    // スクロールの場合、低い速度から慣性を開始/停止したい場合があります
    threshold-start = <2>;
    threshold-stop = <1>;
};
```
### **4\. Integration into the Input Processor Pipeline**

zmk,input-listenerノード内のinput-processorsリストに、設定した慣性プロセッサのインスタンスを追加します。

⚠️ 重要:  
この慣性プロセッサは、処理を完了したマウス・スクロールイベントを次のプロセッサへ転送しません。したがって、必ず入力プロセッサパイプラインの最後に追加してください。
#### **Example: Applying Inertia to Mouse Movement**
```
/ {
    trackpad_input_listener: trackpad_input_listener {
        compatible = "zmk,input-listener";
        // ... (他の設定)
        input-processors = <&zip_xy_scaler 1 1>, // 例: スケーラー
                           <&zip_inertia>;       // 慣性プロセッサは最後に配置
        // ...
    };
};
```
#### **Example: Applying Inertia to Scrolling**

```
/ {
    trackpad_input_listener: trackpad_input_listener {
        // ...
        scroller {
            // ... (他の設定)
            input-processors = <&zip_xy_transform (INPUT_TRANSFORM_Y_INVERT)>,
                               <&zip_xy_scaler 1 10>,
                               <&zip_xy_to_scroll_mapper>,
                               <&zip_inertia_scroll>; // スクロールモードの慣性プロセッサは最後に配置
        };
    };
};
```
## **⚙️ DTS Property Reference**

| プロパティ名       | 型      | デフォルト値 | 説明                                                                                                                    |
|:-------------------|:--------|:-------------|:------------------------------------------------------------------------------------------------------------------------|
| decay-factor-int   | uint16_t  | 90           | 慣性の減衰係数（0-100）。100に近づくほど減速が遅くなります。                                                            |
| report-interval-ms | uint16_t  | 35           | 慣性移動中のHIDレポートの送信間隔（ミリ秒）。ポインティングデバイスの**ポーリングレート以上**に設定する必要があります。 |
| threshold-start    | uint16_t  | 15           | 慣性移動を開始するために必要な入力速度の最小しきい値。                                                                  |
| threshold-stop     | uint16_t  | 1            | 慣性移動を終了する速度のしきい値（この値以下で停止）。                                                                  |
| scroll-mode        | boolean | false        | trueの場合、マウス移動ではなくスクロールHIDレポートを送信します。                                                       |


## **📖 技術的な詳細**

### **📐 Q8固定小数点減衰関数**

慣性の減衰計算には、以下のQ8固定小数点演算が使用されています。

* **減衰計算の仕組み:**  
  1. 現在の整数速度 (in\_dx, in\_dy) をQ8形式に変換し、前回の**剰余** (\*rem\_x, \*rem\_y) を加算して「真の」Q8速度を求めます。  
  2. このQ8速度に、設定された減衰係数 (decay\_factor\_q8) を乗算し、結果を $2^8$ で割って減衰を適用します。  
  3. 減衰後のQ8値から、整数部を四捨五入して出力速度 (\*out\_dx, \*out\_dy) とし、小数部を次のティックのための**剰余**として保存します。

この手法により、減衰処理における**小数点以下の微小な動き**を蓄積し、やがて整数値として出力することで、より**スムーズで正確な減速**を実現しています。
