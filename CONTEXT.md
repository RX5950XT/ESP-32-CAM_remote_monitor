# CONTEXT — ESP32-CAM 遠端監控

## 專案摘要

ESP32-CAM（AI-Thinker / OV2640 + PSRAM）+ Telegram Bot：手動拍照、定期監控、**板端 TFLite 人物偵測**。無雲端 API。

## 目前狀態

- 官方 TFLite Micro `person_detection`（~300 KB，96×96 灰階，int8）
- 雙條件門檻：`PERSON_MIN_SCORE=0.70`、`PERSON_SCORE_MARGIN=0.20`
- 警報冷卻：`ALERT_COOLDOWN_SEC=0`（關閉；改 `config.h` 重燒，非 Bot 指令）
- 排程：做完再計時；同 loop 只跑一個重型任務（**AI 優先**於 monitor）
- 相機：lazy format（連續 AI 可留灰階；JPEG 需求時才切）
- `platform = espressif32@6.12.0` + `TensorFlowLite_ESP32`；上傳埠 **COM3**

## 架構

```
/photo, /monitor_*  → capturePhoto(JPEG，需要時才切 format) → Telegram
/ai_*               → captureGray96（可停留灰階）→ TFLite Invoke
                    → 達標且 cooldown 未擋 → VGA JPEG 警報
```

## 間隔與門檻

| 項目 | 值 |
|------|-----|
| 預設 monitor / AI 間隔 | 各 10 秒 |
| 間隔範圍 | 3～86400 秒 |
| 警報冷卻 | 0（關） |
| person 判定 | `person > no_person + margin` **且** `person >= min_score` |

## 關鍵檔案

| 檔案 | 說明 |
|------|------|
| `src/main.cpp` | setup/loop、互斥重型任務、cooldown |
| `src/scheduler.*` | due + markRunning/Done |
| `src/camera.*` | JPEG / 灰階 lazy 切換、config 零初始化、init 連敗重啟 |
| `src/ai_monitor.*` | TFLite init + 推論 |
| `src/telegram.*` | Bot 指令與送圖 |
| `src/config.h` | secrets + 門檻（gitignore） |
| `platformio.ini` | platform pin、huge_app、COM3 |

## 建置限制

- **必須** `espressif32@6.12.0`（Arduino 2.x）；新版 platform 54.x 會讓 TFLite 編譯失敗
- Partition：`huge_app.csv`；需 PSRAM 較穩
- 驗證：`pio run` → `pio run -t upload` → Serial 115200 見 `Local person_detection ready`

## 已知限制

- TinyML 準度有限（遠距／暗處／雜物易誤判）
- 警報 JPEG ≠ 模型輸入幀
- `setInsecure()` 不做 TLS 憑證驗證（家用簡化）
- monitor + AI 皆短間隔時仍會忙碌，但不會同 loop 連打兩套

## Agent 文件

- `CLAUDE.md` / `AGENTS.md`：專案通用規範（兩者對齊）
- 本檔：交接現況與實作細節
