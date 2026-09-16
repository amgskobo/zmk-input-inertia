# ZMK Inertia Input Processor (日本語版)

このモジュールは、ZMKの**入力処理パイプライン**に**マウスの慣性（Inertia）効果を追加します。トラックパッドやトラックボールなどの相対移動入力が終了した後、設定された減衰率に従って動きを継続**させ、自然な慣性スクロールやマウス移動を実現します。

相対座標イベント (`INPUT_EV_REL`) のみを利用して動作するため、標準的なトラックパッドやトラックボールであれば機種を問わず利用可能です。

---

## ✨ 機能概要

* **慣性移動/スクロール:** 入力イベント（マウス移動またはスクロール）が停止した後、動きを徐々に減衰させながら継続します。
* **Q8固定小数点演算:** 浮動小数点演算を用いない軽量な実装により、MCUへの負荷を最小限に抑えつつ、滑らかな減衰挙動を実現しています。
* **個別設定:** マウス移動とスクロールで独立したパラメータ（減衰率、更新間隔、しきい値）を持っています。

---

## 🛠️ インストールと設定

### 1. モジュールの組み込み

プロジェクトの `config/west.yml` ファイルに以下を追加します。

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

### 2. DTSインクルード

キーボードのDTSファイル（例: `boards/arm/my_keyboard/my_keyboard.dts`）に以下の行を追加します。

```dts
#include <zmk-input-inertia/input/processor/input_inertia.dtsi>
```

### 3. DTSインスタンス設定

必要に応じて、慣性プロセッサの設定を調整します。

```dts
&zip_inertia {
    // 慣性が始まるまでの待ち時間 (推奨: センサーのポーリングレートの2倍以上)
    trigger-ms = <35>;

    // --- マウス移動の設定 ---
    move-decay-factor-int = <90>;       // 更新ごとに保持する速度 (0-99%)
    move-report-interval-ms = <35>;     // レポート送信間隔 (ms)
    move-threshold-start = <15>;        // 慣性が発動する最小移動量 (pix/report)
    move-threshold-stop = <1>;          // 慣性が停止する移動量 (pix/report)

    // --- スクロールの設定 ---
    scroll-decay-factor-int = <85>;    // スクロール減衰率
    scroll-report-interval-ms = <65>;  // スクロールレポート間隔 (ms)
    scroll-threshold-start = <2>;      // 慣性が発動する最小スクロール量 (pix/report)
    scroll-threshold-stop = <0>;       // 慣性が停止するスクロール量 (pix/report)

    // 任意: Ctrl 押下中はスクロール慣性を停止/抑止する
    cancel-scroll-inertia-on-ctrl;
};
```

### 4. パイプラインへの追加

`zmk,input-listener` の `input-processors` リストの**最後**に追加してください。

この慣性プロセッサは、慣性発動中に生成されるイベントを次のインプットプロセッサに渡さず、直接HIDエンドポイントへ送信します。そのため、移動量のスケーリングやスクロールへの変換などの処理がすべて完了した「最終的な入力値」を受け取れるように、必ず入力プロセッサパイプラインの**最後**に配置してください。

```dts
&trackball_listener {
    // 1. 通常のマウス移動 (デフォルト)
    input-processors = <&zip_xy_scaler 1 1>,
                       <&zip_inertia>; // ★同一ノードを参照

    // 2. スクロールモード (例: レイヤー1有効時)
    scroll {
        layers = <1>;
        input-processors = <&zip_xy_to_scroll_mapper>, // 移動をスクロールに変換
                           <&zip_inertia>; // ★同一ノードを参照 (重要: 同じノードを指定)
    };
};
```

> [!IMPORTANT]
> **スクロール・マウス移動との共有**
> 慣性スクロール中にマウスカーソルを動かした際（またはその逆）、即座に慣性を停止させるために、**スクロールとマウス移動は同一のデバイスツリーノード（&zip_inertia）を参照する必要があります。** これにより、異なる操作が行われたことを検知し、自然な停止処理が行われます。

---

## 🚀 最適化ガイド

### **「2倍の法則」 (trigger-ms)**

スムーズな操作感のために、`trigger-ms` の設定が非常に重要です。

* **課題:** ZMKはX軸とY軸の移動を個別のイベントとして処理します。処理の揺らぎ（ジッター）により、次のパケットが数ms遅れることがあります。
* **解決策:** `trigger-ms` をセンサーのポーリング間隔の**2倍以上**に設定してください。
  * 例: 15msセンサー（Xiao BLEなど）の場合、**30ms** または **35ms** を推奨します。
* **理由:** センサーのレポート間隔のばらつきや処理タイミングの微細なズレにより、操作継続中にも関わらず「停止」と誤判定されるのを防ぐためです。この猶予を持たせることで、操作中の意図しない慣性発動（カーソルの暴れやカクつき）を確実に回避します。

---

## 📖 技術的な詳細

慣性処理において、速度が「1」を下回った際に情報を切り捨ててしまうと、動きが不自然に途切れてしまいます。
本モジュールでは、Q8固定小数点演算を使い、更新間で1カウント未満の動きを保持します。

1. **理想速度の構築 (Expansion):**
    入力速度をQ8形式（256倍）に拡張し、前回の計算で生じた「余剰（Remainder）」を加算します。
    `理想速度(Q8) = 入力速度 * 256 + 余剰`
2. **減衰の適用 (Decay):**
    この「理想速度」全体に対して減衰率を乗算します。これにより、整数部だけでなく余剰部分も含めて正確に減衰されます。
3. **出力値の決定 (Rounding):**
    減衰後の値を最も近い整数へ丸め、ちょうど中間の場合はゼロから遠い側を選びます。正方向と負方向で対称な軌跡になります。
4. **余剰の更新 (Update Remainder):**
    `減衰後の値(Q8) - 出力値(Q8)` を次回の余剰として保存します。Q8の量子化誤差を更新ごとに捨てず、次の計算へ持ち越します。

### ⚡ なぜ高速なのか？

ZMKが動作する多くの組み込みMCUは、浮動小数点演算（float/double）をハードウェアで処理する機能が限定的です。
ホットパスは32-bit整数演算だけで処理します。百分率からQ8への変換はビルド時に解決され、workの予約も軸ごとではなく、完成した入力フレームごとに1回だけ行います。

## Configuration Reference

| プロパティ | 型 | 既定値 | 説明 |
| :--- | :--- | :--- | :--- |
| `trigger-ms` | int | 35 | 手動入力が止まってから慣性が始まるまでの遅延。範囲は1〜65535 ms。センサーのポーリング間隔の2倍以上を推奨します。 |
| `move-decay-factor-int` | int | 90 | レポート間隔ごとに保持する速度。範囲は0〜99%。大きいほど滑ります。 |
| `move-report-interval-ms` | int | 35 | 慣性移動レポートの間隔。範囲は1〜65535 ms。 |
| `move-threshold-start` | int | 15 | 慣性開始に必要な直前の手動フレームの絶対差分。範囲は1〜32767。 |
| `move-threshold-stop` | int | 1 | 両軸がこの値以下になると停止。範囲は0以上かつ`move-threshold-start`未満。 |
| `scroll-decay-factor-int` | int | 85 | スクロールで保持する速度。範囲は0〜99%。 |
| `scroll-report-interval-ms` | int | 65 | スクロール慣性の更新間隔。範囲は1〜65535 ms。 |
| `scroll-threshold-start` | int | 2 | スクロール慣性開始に必要な絶対差分。範囲は1〜32767。 |
| `scroll-threshold-stop` | int | 0 | 両軸がこの値以下になると停止。範囲は0以上かつ`scroll-threshold-start`未満。 |
| `cancel-scroll-inertia-on-ctrl` | bool | false | Ctrl 押下中はスクロール慣性を停止し、新たな慣性も抑止します。Ctrl+ホイールによる意図しないズームを防げます。 |

### decay factor の範囲

decay factorは百分率で、ビルド時に範囲検査されます。

| 値 | 挙動 |
| :--- | :--- |
| `0` | trigger発火時に慣性レポートを送らず停止します |
| `1`〜`99` | 速度が減衰し、小さいほど速く止まります |
| `100`以上 | 慣性が収束しないため、ビルド時に拒否されます |

不正な更新間隔、しきい値の範囲、開始・停止しきい値の順序もビルド時に拒否されます。

## 並行処理と複数listener

X/Yイベントは`sync`が付いたイベントで入力フレームが閉じるまで蓄積されます。deviceが送らなかった軸は、そのフレームでは0として扱います。ZMK input listenerごとにフレームと速度履歴を分離するため、2つのdeviceの軸が誤って混ざることはありません。

手動のpointer／scroll入力は、listenerをまたいで競合する予約中・実行中の慣性を停止します。停止時にはatomic generation counterを更新します。遅延callbackはreport送信前にgenerationを再確認し、処理中に手動入力が状態を変更していた場合は古い出力を捨てます。

減衰callbackとそこからのHID reportは、Zephyrのsystem work queueではなく、
ZMKの共有low-priority work queueで実行します。慣性が連続している間も、Bluetooth、
split、watchdogなどのsystem workを遅延させません。

## テスト

依存のない算術・フレームテスト:

```sh
bash ./tests/run-docker.sh
```

upstream ZMKビルドとdevicetree guardテスト:

```sh
bash ./tests/run-integration-docker.sh
```

coreテストは最適化、ASan/UBSan、32-bitの3構成で実行します。統合テストは最新upstream ZMKに対してビルドし、危険なdevicetree値が想定した理由で失敗することを確認します。

## ライセンス

このプロジェクトは[MIT License](./LICENSE)で公開されています。
