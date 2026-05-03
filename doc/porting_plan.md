# esp_8_bit → Narya 2-MCU ボード移植 計画 (NES 専用)

## Context

`/home/kishima/dev/esp_8_bit_for_narya/` は core/ と hid/ が空、Rakefile と tmp/ の参考実装のみ存在する未着手リポジトリ。CLAUDE.md に従い、ESP32-WROVER (core) + ESP32-S3 (hid) の 2-MCU 構成へ esp_8_bit を移植する。映像は NTSC コンポジット (内蔵 DAC GPIO25)、音声は I2S 出力 (fmruby-graphics-audio 流)、入力は S3 から UART 受信に置き換える。**NES (Nofrendo) のみを対象**とし、Atari/SMS のコードパスは取り込まない。実装方針は **Pure ESP-IDF v5.x 書き直し**、tmp/ からは **vendor (コピー) で取り込み**、ピンアサインは **Narya ボード仕様** に合わせる。

## 確定事項

- core: ESP32-WROVER, 4MB PSRAM, NES (Nofrendo) のみ
- hid: ESP32-S3, USB-HOST で Gamepad → UART で core に送信
- フレームワーク: 両方とも ESP-IDF v5.x (Arduino-as-component は使わない)
- ROM 配置: LittleFS パーティションに焼き込み (`media.h` の 1.9MB 巨大 ROM テーブルは取り込まない)
- UART プロトコル: 5バイトフレーム `[0xAA][type:1][seq+len:1][payload:?][crc8]`、ACK なし・再送なし (HID は state-driven なのでロス許容)

## ピンアサイン (Narya 準拠)

[core/main/include/narya_pin_assign.h](../core/main/include/narya_pin_assign.h):
- `NARYA_CORE_VIDEO_DAC = GPIO_NUM_25` (NTSC 出力, 内蔵 DAC1)
- `NARYA_CORE_I2S_BCK = GPIO_NUM_32`, `WS = GPIO_NUM_33`, `DOUT = GPIO_NUM_27`
- `NARYA_CORE_HID_UART_PORT = UART_NUM_1`, `RX = GPIO_NUM_21`, `TX = GPIO_NUM_18`

[hid/main/include/narya_pin_assign.h](../hid/main/include/narya_pin_assign.h):
- `NARYA_HID_USB_DM = GPIO_NUM_19`, `DP = GPIO_NUM_20`, `VBUS_EN = GPIO_NUM_1`
- `NARYA_HID_UART_PORT = UART_NUM_1`, `TX = GPIO_NUM_11`, `RX = GPIO_NUM_13`

## 実装フェーズ

### P-1. プラン文書の永続化
- 本プランの内容を [doc/porting_plan.md](porting_plan.md) として書き出す (リポジトリ内に常駐させ、後続作業の参照点とする)。

### P0. 雛形 + ビルドパイプライン
- `core/CMakeLists.txt`, `core/main/CMakeLists.txt`, `core/main/main.cpp`, `core/Rakefile`, `core/sdkconfig.defaults`, `core/partitions.csv` を新規作成。target=esp32。
- 同構成で `hid/` 側も作成。target=esp32s3。
- ルート `Rakefile` の `rake build` を `core` → `hid` の順に各サブ Rakefile (`idf.py build`) を呼ぶよう調整。既存の `execute_task_in_repos` を流用するが、`.repos` 読み込みではなく固定リスト `["core", "hid"]` を渡す。
- 完了条件: `rake build` で両プロジェクトの空 app_main がビルドできる。

### P1. ピンアサイン + UART プロトコルヘッダ
- 上記 `narya_pin_assign.h` を 2 つ作成。
- 共通プロトコルヘッダ `core/main/include/hid_uart_proto.h` と `hid/main/include/hid_uart_proto.h` (内容は同一、コピーで保持):
  - `enum narya_hid_evt_t { NARYA_EVT_BTN_DOWN=1, NARYA_EVT_BTN_UP=2, NARYA_EVT_AXIS=3, NARYA_EVT_HEARTBEAT=0xF0 }`
  - `struct narya_hid_frame_t` (5 バイト固定)
  - CRC8 (poly=0x07, init=0x00) のテーブルレス実装

### P2. core: NTSC 映像出力
- vendor: `tmp/esp_8_bit/src/video_out.h` → `core/main/video/video_out.h`。ライセンスヘッダ保持。
  - `VIDEO_PIN` を `narya_pin_assign.h` 経由に置換
  - LEDC PWM 関連 (170-172 行) を削除
  - `video_isr` 内の `audio_sample()` 呼び出し (794-796 行) を削除
  - `_audio_buffer` / `audio_write_16` (709-728 行) を削除 (音声は I2S に分離)
- ESP-IDF v5.x では legacy `driver/i2s.h` (deprecation 警告は出るが利用可) を使用し、APLL + I2S0 + 内蔵 DAC のチェーンを温存。`periph_module_enable` の include は `esp_private/periph_ctrl.h` に変わっている点に注意。
- `core/main/video/video_test_main.cpp` でテストパターン (esp_8_bit の `test_wave` 相当) を表示し、APLL 動作を独立検証する。
- 完了条件: TV にテストバーが安定表示される。

### P3. core: I2S 音声出力
- 参考 (構造のみ流用、コピーはしない): `tmp/fmruby-graphics-audio/components/apu_emu/src/apu_if.cpp` の 53-100 行 (init), 102-138 行 (mono→stereo push)。
- 新規作成 `core/main/audio/audio_i2s.cpp` / `core/main/audio/audio_i2s.h`:
  - `audio_i2s_init()`: ESP-IDF v5 の `i2s_new_channel` + `i2s_std_config_t`、I2S_NUM_1、master、15720 Hz、stereo 16-bit
  - `audio_i2s_write_mono(const int16_t *samples, int n)`: ステレオ展開 + `i2s_channel_write(..., portMAX_DELAY)` で背圧
  - DMA: `dma_desc_num=4`, `dma_frame_num=600` (~150 ms キュー)
- 完了条件: 1 kHz サイン波が GPIO27 経由のスピーカで鳴る。

### P4. core: NES (Nofrendo) エミュレータ
- vendor:
  - `tmp/esp_8_bit/src/emu.h` → `core/main/emu/emu.h`。`hid_server.h` include を `narya_hid.h` (新規) に差し替え。
  - `tmp/esp_8_bit/src/emu.cpp` → `core/main/emu/emu.cpp`。CrapFS / app1 マッピング (64-119 行) を LittleFS による mmap-into-PSRAM に書き換え。Atari/SMS 関連分岐は削除。
  - `tmp/esp_8_bit/src/emu_nofrendo.cpp` → `core/main/emu/emu_nofrendo.cpp`。Wii/IR 入力分岐 (360-388 行) を削除し、`event(narya_hid_evt_t)` で `_nes_1[]` を更新する単純化版に置換。
  - `tmp/esp_8_bit/src/nofrendo/` のサブツリー全体 → `core/main/emu/nofrendo/`。`mapNNN.c`, `nes*.c`, `nes6502.c`, `vid_drv.c`, `osd.c` 等。`IRAM_ATTR` はそのまま保持。
- 新規:
  - `core/main/emu/narya_hid.h`: hid_server.h 互換の `KEY_MOD_*` / `GENERIC_*` マクロ最小セット。
  - `core/main/emu/nofrendo_osd_narya.c`: nofrendo の `osd_*` シンボルを Narya 環境に橋渡し (`osd_init`, `osd_getsoundinfo`(15720 Hz), `osd_getromdata`, `osd_installtimer`, `osd_event`, `osd_logprint`)。契約は emu_nofrendo.cpp:155-205 から。
  - `core/data/nofrendo/sokoban.nes` を 1 本配置 (実行時の動作確認用、ライセンスフリーの自作 ROM 推奨)。
  - `core/CMakeLists.txt` で `littlefs_create_partition_image(storage data/nofrendo FLASH_IN_PROJECT)` を呼び ROM を焼き込む。
- 取り込まないもの:
  - `tmp/esp_8_bit/src/media.h` (1.9 MB、Atari/SMS 含む)
  - `emu_atari800.cpp` / `emu_smsplus.cpp` 系: NES 専用なので一切持ち込まない
  - `gui.cpp`: 起動メニューは将来課題、初期は ROM 直接ロード
  - `ir_input.h` / `hid_server/`: UART 入力に置き換える
- 音声経路: `emu_task` がフレームごとに `EmuNofrendo::audio_buffer(int16_t*, n)` (emu_nofrendo.cpp:457-469) を呼び、結果を `audio_i2s_write_mono` に流す。video ISR からの結合は完全に切る。
- 完了条件: TV に NES のタイトル画面が表示され、音声が鳴る (入力は P7 で接続)。

### P5. hid: USB-HOST Gamepad
- vendor:
  - `tmp/fmruby-core/main/drivers/usb/usb_task.c` → `hid/main/usb/usb_task.c`。`fmrb_host_send_gamepad_button` / `_axis` (862, 875 行) の呼び出しを `narya_uart_send_btn` / `_axis` (P6 で定義) に置換。
  - `tmp/fmruby-core/main/drivers/usb/hid_report_parser.{c,h}`, `hid_device_config.{c,h}`, `fmrb_keymap.{c,h}` → 同階層へ。
  - `espressif/usb_host_hid` は `hid/main/idf_component.yml` に dependency 宣言してコンポーネントマネージャ経由で取得 (managed_components/ ツリー全コピーは過大なので採用しない)。
- 新規:
  - `hid/main/include/fmrb_compat.h`: `fmrb_enter_critical` → `portENTER_CRITICAL_SAFE`、`FMRB_LOGD` → `ESP_LOGD` の薄いシム。vendored `usb_task.c` を極力触らないため。
  - `hid/main/main.c`: `app_main` で VBUS 有効化 → `usb_host_install` → `usb_task` 起動 → 1 Hz ハートビート送出。
- 完了条件: ゲームパッドのボタン押下が S3 のシリアルコンソールに出る。

### P6. UART リンク
- 新規:
  - `core/main/transport/hid_uart.c` / `.h`: `uart_driver_install(UART_NUM_1, 1024, 0, 8, &queue, 0)` + 4 状態フレーマ + デコード結果を FreeRTOS キューへ。
  - `hid/main/transport/hid_uart.c` / `.h`: `narya_uart_send_btn(idx, down)` / `narya_uart_send_axis(axis, v)` で 5 バイトフレームを `uart_write_bytes`。
- `fmrb_transport` は意図的に取り込まない (ACK/再送は HID 用途には過剰)。
- 完了条件: パッドのボタン押下が core 側のログに 1:1 で出る。

### P7. 統合 (ファーストライト)
- P6 のキューを `EmuNofrendo::event()` に接続。
- 1 Hz の perf ログ出力: `frame=N emu_us=X audio_q=Y uart_rx=Z uart_drop=W`。
- 完了条件: TV に NES タイトル → START 押下 → ゲーム開始、まで通る。

## FreeRTOS タスク構成 (core)

| Task | Stack | Prio | Core | 役割 |
|------|-------|------|------|------|
| `emu_task` | 6 KB (PSRAM 可) | 4 | 0 | Nofrendo 1 フレーム実行 + 音声 push |
| `video_task` | 2 KB | 5 | 1 | `start_dma` 呼び出し主体 (ISR を core 1 にバインド) + フレーム同期 |
| `hid_uart_rx_task` | 3 KB | 6 | 1 | UART1 受信 → イベントキュー |

`start_dma` は core 1 にピン留めしたタスクから呼び、`video_isr` を core 1 に確実にバインドする (esp_8_bit の dual-core 動作 `esp_8_bit.ino:124` を踏襲)。

## sdkconfig.defaults / partitions.csv

新規設計せず、参考実装からそのままコピーする。CLAUDE.md の「sdkconfig は編集禁止、必要なら提案」に整合する初期値として扱う。

### core (ESP32-WROVER)
- `tmp/fmruby-graphics-audio/sdkconfig.defaults` → `core/sdkconfig.defaults`
- `tmp/fmruby-graphics-audio/partitions.csv` → `core/partitions.csv`
- 既に NTSC (CVBS) + I2S 音声 + PSRAM + LittleFS を有効化済みのはずで、本プロジェクトとほぼ要件一致。差分が必要になった場合のみユーザに提案する。

### hid (ESP32-S3, Narya N16R8)
- `tmp/fmruby-core/config/sdkconfig.defaults.n16r8` → `hid/sdkconfig.defaults`
- `tmp/fmruby-core/config/partitions_n16r8.csv` → `hid/partitions.csv`
- USB Host + N16R8 (16MB flash / 8MB PSRAM) 向けに調整済みのため流用が妥当。N8R8 ボードの場合は `n8r8` 版を選択。

## 検証 (Verification)

CLAUDE.md より TV/音声/パッド入力の最終確認はユーザ実機テスト。実装側は以下のログ・スコープ観測で進捗を担保する:

- ログプローブ (シリアルで grep 可能):
  - `[video] init ok line_width=%d cc=%d apll_locked=%d`
  - `[i2s] init ok bck=%d ws=%d dout=%d sample_rate=15720`
  - `[littlefs] mounted total=%d used=%d`
  - `[emu] rom=%s prg=%dk chr=%dk`
  - 1 Hz 周期: `[perf] frame=%d emu_us=%d audio_q=%d uart_rx=%d uart_drop=%d`
  - `hid_evt: btn=%d %s` (S3→core 受信時)
- スコープ確認 (ユーザ手動): GPIO25 にカラーバースト (~3.58 MHz)、GPIO27 に I2S BCK ~503 kHz。
- UART フレーマ単体テスト: `core/main/transport/hid_uart_test.c` (`-DNARYA_TEST=1` 限定ビルド) で固定バイト列を流し込みデコード結果を assert。
- HID 注入テスト: `hid/main/test_inject.c` (条件コンパイル) で USB なしに 1 Hz でフェイクボタンを送出し、UART リンク単独検証。

### ファーストライト合格基準
TV に NES タイトル画面 → 音声鳴動 → パッド START 押下でゲーム開始まで貫通すること。シリアルに `[littlefs] mounted` `[i2s] init ok` `[video] init ok` `[emu] rom=...` `[perf] frame=...` `hid_evt:` が順次出力されること。

## 主要参照ファイル (移植元)

- `tmp/esp_8_bit/src/video_out.h` — NTSC + I2S0 + APLL + DMA (核心)
- `tmp/esp_8_bit/src/emu.cpp`, `emu.h`, `emu_nofrendo.cpp`
- `tmp/esp_8_bit/src/nofrendo/` — Nofrendo 一式
- `tmp/fmruby-graphics-audio/components/apu_emu/src/apu_if.cpp` — I2S 音声 (init/push のみ参考)
- `tmp/fmruby-core/main/drivers/usb/usb_task.c`, `hid_report_parser.c`, `hid_device_config.c` — USB HID
- `tmp/fmruby-core/components/fmrb_common/include/fmrb_task_config.h` — FreeRTOS タスク優先度参考

## リスクメモ

1. APLL on IDF v5: legacy `driver/i2s.h` を引き続き利用 (deprecation 警告のみ非致命)。`periph_module_enable` の include 先が `esp_private/periph_ctrl.h` に移動しているので追従。
2. I2S0 / I2S1 共存: I2S0=映像 DAC parallel, I2S1=音声 standard。fmruby-graphics-audio で実証済みの組み合わせ。
3. PSRAM 配置: ROM は PSRAM (`MALLOC_CAP_SPIRAM`)、ライン DMA バッファは DRAM (`MALLOC_CAP_DMA`)。
4. 音声と video ISR の脱結合: 旧設計はスキャンライン毎の 1 サンプル供給 (`video_out.h:794-796`)。I2S 化に伴い、フレーム単位 (~262 サンプル) を `i2s_channel_write` で push に変更。DMA キューが揺らぎ吸収。
5. ROM ライセンス: 配布する `sokoban.nes` 等は自作・パブリックドメインに限定。商用 ROM は持ち込まない。
