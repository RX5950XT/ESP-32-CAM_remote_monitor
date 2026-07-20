# ESP32-CAM 遠端監控系統

基於 ESP32-CAM 的遠端監控系統，透過 Telegram Bot 控制拍照、定期監控與**本地 TinyML 人物偵測**。偵測到畫面中有人時，自動回傳警報照片。

## 功能特色

- **手動拍照** — Telegram 指令即時拍照回傳
- **定期監控** — 定時 JPEG 上傳（預設 10 秒，可調 3～86400）
- **本地人物偵測** — 板端官方 TFLite Micro `person_detection`，無需雲端 API
- **低光源優化** — OV2640 曝光／增益調整
- **穩定排程** — 重型任務互斥、完成後再計時、相機 format lazy 切換

## 硬體需求

| 元件 | 說明 |
|------|------|
| ESP32-CAM | OV2640 + **PSRAM**（強烈建議） |
| 燒錄底板 | USB 供電與下載 |

> 本地模型 tensor arena ~100 KB 優先放 PSRAM。無 PSRAM 時 AI 初始化可能失敗。

## 軟體需求

- [PlatformIO](https://platformio.org/)
- WiFi、[Telegram Bot Token](https://core.telegram.org/bots#how-do-i-create-a-bot)（@BotFather）
- **不需要** OpenRouter 或其他雲端 API Key

## 快速開始

### 1. 設定金鑰

```bash
cp src/config.h.example src/config.h
```

編輯 `src/config.h`：

```cpp
#define WIFI_SSID "你的WiFi名稱"
#define WIFI_PASSWORD "你的WiFi密碼"
#define BOT_TOKEN "你的Bot Token"
#define CHAT_ID "你的Chat ID"

// 偵測門檻（兩者都要過才警報）
#define PERSON_SCORE_MARGIN 0.20f
#define PERSON_MIN_SCORE 0.70f

// 間隔與冷卻
#define DEFAULT_MONITOR_INTERVAL_SEC 10
#define DEFAULT_AI_INTERVAL_SEC 10
#define MIN_MONITOR_INTERVAL_SEC 3
#define MAX_MONITOR_INTERVAL_SEC 86400
#define MIN_AI_INTERVAL_SEC 3
#define MAX_AI_INTERVAL_SEC 86400
#define ALERT_COOLDOWN_SEC 0   // 0=關閉；改此值需重燒（非 Telegram 指令）
```

> `config.h` 已在 `.gitignore`。

### 2. 取得 Chat ID

與 `@userinfobot` 對話取得數字 ID，填入 `CHAT_ID`。

### 3. 編譯與燒錄

```bash
pio run -t upload
```

預設埠 `COM3`（見 `platformio.ini`）。啟動後 Telegram 會收到開機訊息。

## Telegram 指令

| 指令 | 功能 |
|------|------|
| `/start` | 說明 |
| `/photo` | 立即拍照（SVGA） |
| `/monitor_on` / `/monitor_off` | 定期監控開關 |
| `/monitor_interval <秒>` | 監控間隔（3～86400） |
| `/ai_on` / `/ai_off` | 本地人物偵測開關 |
| `/ai_interval <秒>` | 偵測間隔（3～86400） |
| `/status` | 狀態（含警報冷卻設定） |

## 本地人物偵測

### 流程

1. 依間隔取 **96×96 灰階**（已在灰階則不 reinit）
2. uint8→int8（`^ 0x80`）餵入 TFLite `person_detection`
3. 同時滿足 margin + min_score → 視為有人
4. 若 `ALERT_COOLDOWN_SEC` 允許 → 再拍 VGA JPEG 警報
5. 無人則靜默；相機可維持灰階直到下次需要 JPEG

### 模型

| 項目 | 內容 |
|------|------|
| 模型 | TFLite Micro person_detection（int8，~300 KB） |
| 輸入 | 96×96 灰階 |
| 輸出 | no_person / person |
| 成本 | 離線、零 API 費 |

### 準度

TinyML 上限有限。空房誤報可提高 `PERSON_MIN_SCORE`；漏報則略降。警報照與模型輸入**不是同一幀**。

## 專案結構

```
ESP-32-CAM_remote_monitor/
├── platformio.ini
├── README.md / CONTEXT.md / CLAUDE.md / AGENTS.md
├── src/
│   ├── config.h.example / config.h
│   ├── main.cpp
│   ├── camera.* / telegram.* / ai_monitor.* / scheduler.*
│   ├── model_settings.h
│   └── person_detect_model_data.*
└── ...
```

| 模組 | 職責 |
|------|------|
| main | 初始化、loop、任務互斥、cooldown |
| camera | JPEG／灰階 lazy 切換 |
| telegram | Bot 與送圖 |
| ai_monitor | TFLite 推論 |
| scheduler | 間隔 due + 完成後計時 |

## 建置注意

- **必須** `platform = espressif32@6.12.0`（Arduino 2.x + TensorFlowLite_ESP32）
- Partition：`huge_app.csv`
- 上傳埠預設 `COM3`，Serial `115200`

## 依賴

| 套件 | 用途 |
|------|------|
| ArduinoJson v7 | Telegram 依賴 |
| UniversalTelegramBot | Bot API |
| TensorFlowLite_ESP32 | 本地推論 |

## 常見問題

**照片太暗** — 調 `camera.cpp` 的 `applyLowLightSettings()`（`ae_level`、`gainceiling`）。

**一直報有人但照片沒人** — 調高 `PERSON_MIN_SCORE`／`PERSON_SCORE_MARGIN`；看 Serial `person`／`no_person`／`gray_mean`。

**燒錄後沒反應** — 重上電；Serial 115200；確認 Telegram 開機訊息。

**platform 編譯失敗** — 勿升到 Arduino 3.x／platform 54.x，鎖定 `espressif32@6.12.0`。
