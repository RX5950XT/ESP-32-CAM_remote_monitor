# ESP32-CAM 遠端監控系統

基於 ESP32-CAM 的遠端監控系統，透過 Telegram Bot 控制拍照、定期監控與 AI 人物偵測功能。當 AI 偵測到畫面中有人時，自動回傳照片並發出警報。

## 功能特色

- **手動拍照** — 透過 Telegram 指令即時拍照回傳
- **定期監控** — 自動定時拍照並傳送至 Telegram（預設間隔 5 分鐘）
- **AI 人物偵測** — 定期拍照並透過 AI 視覺模型分析，偵測到人物時自動發送警報照片
- **低光源優化** — 針對 OV2640 感測器調整曝光、增益等參數，改善暗處拍攝品質
- **Telegram Bot 控制** — 所有功能均可透過 Telegram 指令遠端操作

## 硬體需求

| 元件           | 說明                                    |
| -------------- | --------------------------------------- |
| ESP32-CAM     | 含 OV2640 攝影模組與 PSRAM              |
| ESP32-CAM 底板 | USB 供電與燒錄用（Type-C 或 Micro-USB） |

> **注意：** 系統需要 PSRAM 才能正常運作（版本內建 4MB PSRAM）。無 PSRAM 時會自動降級為較低解析度，但 AI 偵測功能可能因記憶體不足而不穩定。

## 軟體需求

- [PlatformIO](https://platformio.org/)（推薦使用 VS Code 擴充套件）
- WiFi 網路
- [Telegram Bot Token](https://core.telegram.org/bots#how-do-i-create-a-bot)（透過 @BotFather 建立）
- [OpenRouter API Key](https://openrouter.ai/)（用於 AI 人物偵測）

## 快速開始

### 1. 複製專案

```bash
git clone https://github.com/your-username/Remote_monitor.git
cd Remote_monitor
```

### 2. 設定金鑰

複製範本並填入你的金鑰：

```bash
cp src/config.h.example src/config.h
```

編輯 `src/config.h`：

```cpp
// WiFi
#define WIFI_SSID "你的WiFi名稱"
#define WIFI_PASSWORD "你的WiFi密碼"

// Telegram Bot
#define BOT_TOKEN "你的Bot Token"
#define CHAT_ID "你的Chat ID"

// OpenRouter
#define OPENROUTER_API_KEY "你的OpenRouter API Key"
#define OPENROUTER_MODEL "google/gemma-3-27b-it"

// 預設間隔（秒）
#define DEFAULT_MONITOR_INTERVAL_SEC 300   // 定期監控：5 分鐘
#define DEFAULT_AI_INTERVAL_SEC 60         // AI 偵測：1 分鐘
```

> `config.h` 已加入 `.gitignore`，不會被提交至版本控制。

### 3. 取得 Telegram Chat ID

1. 在 Telegram 搜尋 `@userinfobot` 並啟動對話
2. 它會回覆你的 Chat ID（一串數字）
3. 將此 ID 填入 `config.h` 的 `CHAT_ID`

### 4. 編譯與燒錄

```bash
pio run -t upload
```

燒錄完成後，重新插拔電源讓 ESP32-CAM 重啟。系統啟動成功後，你會在 Telegram 收到：

```
🟢 ESP32-CAM 監控系統已啟動！
輸入 /start 查看可用指令。
```

## Telegram 指令

| 指令                       | 功能                                          |
| -------------------------- | --------------------------------------------- |
| `/start`                 | 顯示可用指令清單                              |
| `/photo`                 | 立即拍照並回傳（SVGA 800×600）               |
| `/monitor_on`            | 開啟定期監控拍照                              |
| `/monitor_off`           | 關閉定期監控拍照                              |
| `/monitor_interval <秒>` | 設定監控拍照間隔（最少 10 秒）                |
| `/ai_on`                 | 開啟 AI 人物偵測                              |
| `/ai_off`                | 關閉 AI 人物偵測                              |
| `/ai_interval <秒>`      | 設定 AI 偵測間隔（最少 10 秒）                |
| `/status`                | 查看系統狀態（開關、間隔、WiFi 訊號、記憶體） |

## AI 人物偵測

### 運作原理

1. 依設定間隔定期拍攝 VGA (640×480) 照片
2. 將 JPEG 圖片以 Base64 編碼
3. 透過 OpenRouter API 發送至視覺語言模型進行分析
4. 模型判斷畫面中是否有人
5. 若偵測到人物 → 自動將照片傳送至 Telegram 並附帶警報訊息
6. 若未偵測到人物 → 靜默丟棄照片，不發送任何通知

### 推薦使用模型

| 模型                            | 說明                                                       |
| ------------------------------- | ---------------------------------------------------------- |
| **google/gemma-3-27b-it** | 推薦使用。支援視覺輸入，回應快速，判斷準確度高，且成本極低 |

你也可以在 `config.h` 中替換為其他支援視覺功能的模型（如 `google/gemini-2.5-flash-lite`、`openai/gpt-4o-mini` 等），只要該模型在 OpenRouter 上支援 image input 即可。

若使用 OpenRouter 的 Preset 功能，可將 `OPENROUTER_MODEL` 設為 `@preset/你的預設名稱`。

### API 成本

使用 `google/gemma-3-27b-it` 模型，**單次照片分析請求的成本約為 $0.0000325 美金**。

參考成本估算：

| AI 偵測間隔 | 每小時請求數 | 每小時成本        | 每日成本 | 每月成本（30 天） |
| ----------- | ------------ | ----------------- | -------- | ----------------- |
| 10 秒       | 360          | $0.0117 | $0.281  | $8.42    |                   |
| 30 秒       | 120          | $0.0039 | $0.094  | $2.81    |                   |
| 60 秒       | 60           | $0.00195 | $0.047 | $1.40    |                   |
| 5 分鐘      | 12           | $0.00039 | $0.009 | $0.28    |                   |

> 以上成本僅計算 OpenRouter API 費用，不含網路流量等其他費用。實際費用可能因圖片大小與模型回應長度略有差異。

## 專案結構

```
Remote_monitor/
├── platformio.ini          # PlatformIO 專案設定
├── .gitignore
├── src/
│   ├── config.h.example    # 設定檔範本
│   ├── config.h            # 實際設定（不進版控）
│   ├── main.cpp            # 主程式入口（setup / loop）
│   ├── camera.h            # 相機模組介面（腳位定義）
│   ├── camera.cpp          # 相機初始化與拍照（含低光源優化）
│   ├── telegram.h          # Telegram 模組介面
│   ├── telegram.cpp        # Telegram Bot 收發訊息與照片
│   ├── ai_monitor.h        # AI 偵測模組介面
│   ├── ai_monitor.cpp      # OpenRouter API 呼叫與人物偵測
│   ├── scheduler.h         # 排程器介面
│   └── scheduler.cpp       # 定時任務排程（millis 計時器）
└── README.md
```

### 模組說明

| 模組                 | 職責                                                                      |
| -------------------- | ------------------------------------------------------------------------- |
| **main**       | 系統初始化（WiFi、NTP、相機、Telegram）、主迴圈整合                       |
| **camera**     | OV2640 感測器初始化、低光源參數調校、拍照與解析度切換                     |
| **telegram**   | Telegram Bot 訊息收發、照片上傳（multipart/form-data 分塊傳送）、指令處理 |
| **ai_monitor** | JPEG → Base64 編碼、OpenRouter API 串接、HTTP 回應解析、人物偵測判斷     |
| **scheduler**  | 基於 `millis()` 的簡易排程器，管理定期監控與 AI 偵測的啟停與間隔        |

## 技術細節

### 相機設定

- **解析度：** 監控拍照 SVGA (800×600)、AI 偵測 VGA (640×480)
- **JPEG 品質：** 10（有 PSRAM 時）、12（無 PSRAM 時）
- **時脈頻率：** 10 MHz（降低以允許更長曝光時間）
- **低光源優化：** 自動曝光 + 自動增益 + 曝光補償 +2 + 增益上限 128x
- **拍攝穩定：** 丟棄前 3 幀 + 額外等待 500ms，讓自動曝光充分適應環境

### 記憶體管理

- JPEG 圖片約 30–80 KB，Base64 編碼後約 40–110 KB
- Base64 緩衝區優先分配在 PSRAM（`ps_malloc`）
- 傳送完畢後立即釋放記憶體
- AI 偵測使用較低解析度 (VGA) 以減少記憶體佔用

### 網路通訊

- 使用 `WiFiClientSecure` 進行 HTTPS 連線（跳過憑證驗證以節省記憶體）
- Telegram 照片上傳採用 multipart/form-data，分塊傳送（1024 bytes/chunk）
- OpenRouter API 請求採用串流方式傳送 JSON，避免大量記憶體配置
- 內建 WiFi 斷線自動重連機制

### 安全性

- Telegram Bot 僅回應指定 Chat ID，拒絕未授權使用者的指令
- `config.h` 包含所有敏感資訊，已加入 `.gitignore` 排除版控

## 依賴套件

| 套件                                                                                  | 用途                             |
| ------------------------------------------------------------------------------------- | -------------------------------- |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) v7                               | JSON 解析（OpenRouter API 回應） |
| [Universal Telegram Bot](https://github.com/witnessmenow/Universal-Arduino-Telegram-Bot) | Telegram Bot API 輪詢與訊息發送  |

## 常見問題

### 拍出來的照片太暗？

系統已針對低光源環境進行優化。若仍然太暗，可在 `camera.cpp` 中調整以下參數：

```cpp
sensor->set_ae_level(sensor, 2);         // 曝光補償，範圍 -2 ~ +2
sensor->set_gainceiling(sensor, (gainceiling_t)6);  // 增益上限，6 = 128x
```

增益上限越高，暗處越亮，但雜訊也會增加。

### AI 偵測結果不準確？

- 確認使用的模型支援視覺輸入（image input）
- 嘗試更換其他視覺模型（如 `google/gemini-2.5-flash-lite`）
- 確保攝影機畫面清晰且光線充足

### 燒錄後沒有反應？

1. 燒錄完成後，先拔掉電源再重新接上
2. 確認 Serial Monitor 鮑率設定為 `115200`
3. 部分 ESP32-CAM 底板的 Serial 輸出可能不穩定，可透過 Telegram 確認系統是否正常啟動（會收到啟動訊息）

### WiFi 連線不穩定？

系統內建自動重連機制。若 WiFi 訊號較弱，可透過 `/status` 指令查看 RSSI 訊號強度，建議維持在 -70 dBm 以上。


