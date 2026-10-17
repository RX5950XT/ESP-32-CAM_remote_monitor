# AGENTS.md — ESP32-CAM Remote Monitor

本檔為 canonical 專案規範；`CLAUDE.md` 僅作相容入口。接手時先讀 `CONTEXT.md`。

## 語言

繁體中文（臺灣）；技術標識可英文。

## 專案

- **類型**：ESP32-CAM 韌體（PlatformIO / Arduino）
- **功能**：自適應拍攝／Telegram 拍照／定期監控／本地 TFLite 人物偵測／區網 MCP
- **非**：雲端 VLM、ESP-WHO（本板非 S3）、Web 後端

## 工作流

1. 讀 `CONTEXT.md` → 再改碼  
2. 改動最小化；修完自行編譯驗證  
3. 更新 `CONTEXT.md`；規範正文僅維護在 `AGENTS.md`
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
- **ai_monitor**：結構化推論結果，明確區分失敗與無人
- **device_control**：兩入口共用設定／操作、NVS 原子保存、最近結果與 health
- **telegram**：授權 chat、間隔 3～86400  
- **mcp_server**：主 loop 串行處理；驗證同子網、金鑰、Host／Origin、輸入及操作間隔

## 設定鍵（config.h）

| 鍵 | 說明 |
|----|------|
| `PERSON_MIN_SCORE` / `PERSON_SCORE_MARGIN` | 首次預設雙門檻；兩入口可調並保存 |
| `ALERT_COOLDOWN_SEC` | 首次預設警報冷卻，0=關；兩入口可調並保存 |
| `DEFAULT_*_INTERVAL_SEC` | 預設間隔 |
| `MIN_*` / `MAX_*` | 間隔上下限 |

## 安全

- Token／WiFi 僅 `config.h`，已 gitignore  
- MCP Token 至少 32 字元；空白則關閉；含連線金鑰的 `mcp.local.json` 也必須 gitignore
- MCP 僅限可信區網，HTTP 未加密，不得自行開放公網
- Bot 只回覆 `CHAT_ID`  
- TLS 目前 `setInsecure()`（已知簡化）

## 完成檢查

- [ ] `pio run` 成功  
- [ ] 相機／MCP 變更執行 host checks；協定變更驗證官方 MCP client
- [ ] 行為與 README／CONTEXT 一致  
- [ ] 未把 secrets 寫進文件或 commit  
