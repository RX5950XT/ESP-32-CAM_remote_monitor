# CLAUDE.md — ESP32-CAM Remote Monitor

## 語言

- 回覆與 README／CONTEXT：繁體中文（臺灣用語）
- 技術術語可保留英文

## 專案性質

PlatformIO + Arduino-ESP32 韌體：Telegram 遠端監控 + **板端 TFLite person_detection**。  
非 Web／非 Node／非 Supabase 專案。

## 動手前

1. 讀 `CONTEXT.md` 掌握現況
2. 追完整資料流再改（camera ↔ AI ↔ telegram ↔ scheduler）
3. 不擴 scope；secrets 不進版控

## 建置硬規則

- **必須** `platform = espressif32@6.12.0`（Arduino 2.x）
- 依賴：`TensorFlowLite_ESP32`、UniversalTelegramBot、ArduinoJson
- Partition：`huge_app.csv`；板子建議有 PSRAM
- 驗證：`pio run`，需要時 `pio run -t upload`（COM3）
- 改完更新 `CONTEXT.md`；`CLAUDE.md` 與 `AGENTS.md` 保持對齊

## 架構慣例

| 模組 | 職責 |
|------|------|
| `main` | setup/loop、重型任務互斥（AI 優先）、警報 cooldown |
| `camera` | JPEG／灰階 lazy format；`camera_config_t` 零初始化 |
| `ai_monitor` | TFLite init／推論；雙門檻判定 |
| `telegram` | 僅 `CHAT_ID`；間隔 min/max 驗證 |
| `scheduler` | due 檢查；`mark*Done` 在**完成後**重計時 |
| `config.h` | secrets + 門檻（gitignore）；`config.h.example` 無真密鑰 |

## 人物偵測

- 輸入 96×96 灰階，`^ 0x80` → int8
- 類別：`no_person=0`，`person=1`
- 警報：`person > no_person + MARGIN` **且** `person >= MIN_SCORE`
- `ALERT_COOLDOWN_SEC` 在 config（0=關），**不是** Telegram 指令
- 警報 JPEG 與推論幀分離

## 禁止／避免

- 硬編碼 secrets 進 git
- 升級 platform 到 Arduino 3.x 而不驗證 TFLite
- 同 loop 連跑 monitor + AI
- 每次 AI 強制 JPEG↔灰階雙向 reinit（應用 lazy）
- 間隔無上限導致 `sec*1000` 溢位

## Git

- Commit：`<type>: <description>`（feat/fix/refactor/docs/chore…）
- 不強制代 commit；使用者要求再提交

## 完成標準

- 編譯通過；有硬體時上傳並確認 Serial／Telegram 行為
- 文件與實作一致（README、CONTEXT、本檔、AGENTS.md）
