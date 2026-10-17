# ESP32-CAM 遠端監控

透過 **Telegram Bot** 或同區網 **MCP Agent** 拍照、調畫質、定期監控與偵測人物。曝光、增益、白平衡會隨光線自動調整；人物偵測在板端執行，不需要雲端 AI API。

兩個入口共用操作與設定，修改後自動保存，重開機仍保留。

## 快速開始

準備 ESP32-CAM（AI-Thinker／OV2640，建議有 PSRAM）、燒錄底板、WiFi，以及 [PlatformIO](https://platformio.org/)。

### 1. 設定連線

首次設定才複製；已有 `src/config.h` 時保留原檔：

```powershell
Copy-Item src/config.h.example src/config.h
```

在 `src/config.h` 填入 WiFi 與 [Telegram Bot](https://core.telegram.org/bots#how-do-i-create-a-bot) 資料：

```cpp
#define WIFI_SSID "你的WiFi名稱"
#define WIFI_PASSWORD "你的WiFi密碼"
#define BOT_TOKEN "你的Bot Token"
#define CHAT_ID "你的Chat ID"
#define MCP_AUTH_TOKEN ""  // 要啟用 MCP，填入獨立隨機金鑰，至少 32 字元
#define DEVICE_NAME ""     // 選填顯示名稱，例如「客廳」；主機名稱仍自動產生
```

MCP 金鑰可用 `python -c "import secrets; print(secrets.token_hex(32))"` 產生，不要沿用 Telegram Token。空白、少於 32 字元或未設定時，只關閉 MCP。

`config.h` 已排除版控。其餘預設值見 [`config.h.example`](src/config.h.example)。

### 2. 編譯與燒錄

```powershell
pio run
pio run -t upload
pio device monitor --port COM3 --baud 115200
```

預設埠 `COM3`。啟動成功後，Telegram 會收到開機訊息；Serial 顯示板子 IP。

建置固定使用 `espressif32@6.12.0`、Arduino 2.x 與 `huge_app.csv`，請保留 [`platformio.ini`](platformio.ini) 設定。燒錄後只需供電和 WiFi，不需要電腦當中繼。

## Telegram／MCP 功能對照

Telegram 只接受 `CHAT_ID`，照片固定傳到該 chat。MCP 共 **8 個工具**：

| 功能 | Telegram | MCP |
|------|----------|-----|
| 拍照回傳 | `/photo [解析度]` | `camera_capture`：回圖給 Agent |
| 拍照傳 Telegram | `/photo [解析度]` | `camera_send_telegram`：回傳送達結果 |
| 調畫質並保存 | `/camera <JSON>` | `camera_configure` |
| 調監控／AI 開關及間隔並保存 | `/monitor <JSON>` | `monitor_configure` |
| 立即偵測一次、取得分數 | `/detect` | `detection_run` |
| 查最近偵測結果與時間 | `/detect_status` | `detection_status` |
| 調人物門檻、警報冷卻並保存 | `/detect_config <JSON>` | `detection_configure` |
| 完整設定、記憶體及錯誤紀錄 | `/status` | `camera_status` |
| 操作說明 | `/start` | `tools/list` |

原有指令保留：`/monitor_on`、`/monitor_off`、`/monitor_interval <秒>`、`/ai_on`、`/ai_off`、`/ai_interval <秒>`。

拍照解析度：`vga`＝640×480、`svga`＝800×600（預設）、`xga`＝1024×768、`uxga`＝1600×1200。MCP 使用 `resolution` 參數；無 PSRAM 時降為 CIF（400×296）。

### 設定參數

Telegram 的 JSON 與 MCP 的 `arguments` 使用相同欄位。三個設定工具至少提供一項；沒傳的欄位保持原值。以下為尚未保存設定時的預設值。

| 設定 | 欄位 | 範圍／預設 |
|------|------|------------|
| 相機 | `brightness`、`contrast`、`saturation` | 整數 -2～2，預設 0 |
| 相機 | `exposure_level` | 整數 -2～2，預設 +1 |
| 相機 | `gain_ceiling` | 0＝2x、1＝4x、2＝8x、3＝16x（預設）、4＝32x |
| 相機 | `jpeg_quality` | 整數 8～20，預設 8；越小越清楚、檔案越大 |
| 監控 | `monitor_enabled`、`ai_enabled` | `true`／`false`，預設關閉 |
| 監控 | `monitor_interval_sec`、`ai_interval_sec` | 整數 3～86400 秒，預設各 10 秒 |
| 偵測 | `person_min_score`、`person_score_margin` | 數值 0～1，預設 0.7／0.2 |
| 偵測 | `alert_cooldown_sec` | 整數 0～86400 秒，預設 0＝關閉冷卻 |

曝光、增益與白平衡持續自動調整。提高增益上限也可能增加雜點，請以實拍比較。

### Telegram 範例

```text
/photo uxga
/camera {"exposure_level":1,"gain_ceiling":3,"jpeg_quality":8}
/monitor {"monitor_enabled":true,"monitor_interval_sec":30,"ai_enabled":false}
/detect_config {"person_min_score":0.7,"person_score_margin":0.2,"alert_cooldown_sec":30}
/detect
/detect_status
/status
```

## 接入 MCP

Agent 必須位於板子的同一子網，支援 **Streamable HTTP** 與自訂 `Authorization` header。每台網址為 `http://esp32cam-<設備ID>.local/mcp`，無法解析時改用 `http://<板子IP>/mcp`。

### 如何知道每台的名稱

名稱依完整 MAC 自動產生，重開機或 IP 改變也不變，**不用查看燒錄內容**：

- **Telegram 開機訊息**：直接列出設備名稱、ID、主機名稱與 MCP 網址。
- **Telegram `/status`**：隨時查詢名稱與目前 IP；所有訊息和照片也標示設備。
- **MCP `camera_status`**：回傳 `device_id`、`device_name`、`hostname`、`mcp_url`、`mcp_ip_url`。
- **Serial 115200**：啟動時列出相同連線資訊；mDNS 公告也包含設備 ID 與 `/mcp` 路徑。

可在 `config.h` 選填 `DEVICE_NAME` 為「客廳」等顯示名稱，不影響唯一主機名稱。多台分別加入 Agent，使用不同連線名稱與各自 MCP 金鑰；Telegram 控制需每台使用不同 Bot Token，可共用同一 `CHAT_ID`。

舊版固定的 `esp32cam.local` 已改為各台名稱，既有客戶端需更新 URL。

### Agent 連線範例

支援以下格式的客戶端，可使用 [`mcp.config.example.json`](mcp.config.example.json)。以下網址是範例，改成開機訊息中的實際網址，金鑰則填各台已燒錄的 `MCP_AUTH_TOKEN`；只有一台時保留一筆即可：

```json
{
  "mcpServers": {
    "living_room": {
      "type": "http",
      "url": "http://esp32cam-246f28a1b2c3.local/mcp",
      "headers": {
        "Authorization": "Bearer REPLACE_WITH_LIVING_ROOM_MCP_AUTH_TOKEN"
      }
    },
    "entrance": {
      "type": "http",
      "url": "http://esp32cam-246f28d4e5f6.local/mcp",
      "headers": {
        "Authorization": "Bearer REPLACE_WITH_ENTRANCE_MCP_AUTH_TOKEN"
      }
    }
  }
}
```

各 Agent 的設定格式可能不同；表單欄位填 Streamable HTTP、上述 URL 與 `Authorization: Bearer <金鑰>`。僅支援 stdio 或舊版 SSE 的客戶端無法直接連線；本服務無 OAuth、SSE 或 session。

本機若有 `mcp.local.json`，它是含金鑰的私有連線檔，不會自動替 Agent 註冊，**不可公開**。新增 Agent 可沿用現有金鑰；修改板端金鑰才需重燒，並同步更新客戶端。

連線後以 `tools/list` 取得 8 個工具及參數。以下為完整 `tools/call` 請求範例；使用 Agent 工具介面時，提供工具名稱與參數即可：

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "camera_capture",
    "arguments": {"resolution": "uxga"}
  }
}
```

**MCP 使用未加密 HTTP，僅限可信任區網，勿轉發上網。** 外網雲端 Agent 無法直接接入。

## 保存、偵測與拍攝行為

- **設定保存**：相機參數、監控／AI 開關及間隔、人物門檻與冷卻自動存入 NVS。已保存的開關若為開啟，重開機會恢復監控；保存失敗會回報並保留原設定。
- **結果紀錄**：偵測結果與健康紀錄重開機即清空，時間以本次開機後秒數表示。狀態包含最近嘗試、成功及歷史錯誤；偵測失敗會明確回報，不當成「無人」。
- **人物警報**：本地 TFLite `person_detection` 使用 96×96 灰階，只有 `person >= person_min_score` 且 `person > no_person + person_score_margin` 才發警報。警報照片為 VGA，與模型輸入不是同一幀；送達後才開始冷卻，失敗則等 30 秒重試。
- **立即偵測**：`/detect`／`detection_run` 只回傳判斷、分數與時間，不改排程、不自動發警報。尚無結果時 `available=false`；失敗時 `success=false`，判斷與分數為 `null`。
- **操作等待**：兩入口共用完成後 3 秒操作間隔，狀態／結果／說明查詢不受限。拍照可能等待曝光穩定約 2～10 秒，超時則回最新照片；AI、Telegram 與拍照依序執行，MCP 客戶端逾時建議至少 60 秒。
- **硬體限制**：固定鏡頭無法自動對焦；暗處可能有雜點或拖影，TinyML 也可能誤判。無 PSRAM 時，人物模型可能無法初始化。

WiFi 與金鑰仍由私有 `config.h` 設定，不提供遠端修改、燒錄、重啟、錄影或串流。Telegram 目前略過 TLS 憑證驗證，屬已知限制。

## 常見問題

| 現象 | 檢查方式 |
|------|----------|
| 找不到設備的 `.local` 名稱 | 從 Telegram 開機訊息或 `/status` 確認網址，或改用目前 IP |
| MCP 無法連線 | 確認供電、WiFi、金鑰已燒錄，且網路未隔離用戶端 |
| HTTP 401 | Bearer token 缺少或與板端不一致 |
| HTTP 403 | 來源非同子網，或 Host／Origin 不符；直接使用該台 IP 或唯一主機名稱 |
| HTTP 405 | 已通過認證，但用了非 POST 方法；`/mcp` 不是網頁 |
| `Busy`／逾時 | 等上次操作完成 3 秒，或調長客戶端逾時 |
| 照片太暗／空房誤報 | 調整曝光／增益或偵測門檻，再用照片與分數確認；極暗仍需補光 |
| 燒錄後沒反應 | 重上電，查看 Serial 115200 與 Telegram 開機訊息 |

## 開發與驗證

```bash
pio run
python test/run_host_checks.py
uv run --no-project --with mcp==2.3.0 test/check_mcp_sdk.py
```

Host checks 需 `g++`，以真實 C++ 搭配硬體替身驗證 Telegram／MCP 對齊、保存及錯誤處理；官方 SDK 測試驗證 8 工具與完整圖片往返。Windows 可在 WSL 執行 host checks，目前 SDK 測試使用 `Ubuntu-24.04`。

這些測試不代表實機畫質或真人辨識準度已驗證。模組說明、Windows 驗證方式及既有實機證據見 [`CONTEXT.md`](CONTEXT.md)，專案規範見 [`AGENTS.md`](AGENTS.md)。
