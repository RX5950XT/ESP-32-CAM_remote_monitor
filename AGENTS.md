# AGENTS.md — ESP32-CAM Remote Monitor

與 `CLAUDE.md` 對齊的通用 Agent 規範。接手時先讀 `CONTEXT.md`。

## 語言

繁體中文（臺灣）；技術標識可英文。

## 專案

- **類型**：ESP32-CAM 韌體（PlatformIO / Arduino）
- **功能**：Telegram 拍照／定期監控／本地 TFLite 人物偵測
- **非**：雲端 VLM、ESP-WHO（本板非 S3）、Web 後端

## 工作流

1. 讀 `CONTEXT.md` → 再改碼  
2. 改動最小化；修完自行編譯驗證  
3. 更新 `CONTEXT.md`；同步 `CLAUDE.md`／`AGENTS.md` 若規範有變  
4. 不提交 `src/config.h`（含 secrets）

## 建置

```text
platform = espressif32@6.12.0   # 強制，勿隨意升級
board = esp32cam
lib: TensorFlowLite_ESP32, UniversalTelegramBot, ArduinoJson
upload_port = COM3（可改）
```

```bash
pio run
pio run -t upload
pio device monitor --port COM3 --baud 115200
```

## 模組邊界

- **main**：loop 內同輪只跑一個重型任務；AI 優先；cooldown  
- **scheduler**：完成後 `mark*Done` 才更新時間戳  
- **camera**：lazy format；config 零初始化；init 連敗可重啟  
- **ai_monitor**：推論與分數門檻  
- **telegram**：授權 chat、間隔 3～86400  

## 設定鍵（config.h）

| 鍵 | 說明 |
|----|------|
| `PERSON_MIN_SCORE` / `PERSON_SCORE_MARGIN` | 偵測雙門檻 |
| `ALERT_COOLDOWN_SEC` | 警報冷卻秒數，0=關（非 Bot 指令） |
| `DEFAULT_*_INTERVAL_SEC` | 預設間隔 |
| `MIN_*` / `MAX_*` | 間隔上下限 |

## 安全

- Token／WiFi 僅 `config.h`，已 gitignore  
- Bot 只回覆 `CHAT_ID`  
- TLS 目前 `setInsecure()`（已知簡化）

## 完成檢查

- [ ] `pio run` 成功  
- [ ] 行為與 README／CONTEXT 一致  
- [ ] 未把 secrets 寫進文件或 commit  
