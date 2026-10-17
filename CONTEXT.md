# CONTEXT — ESP32-CAM 遠端監控

## 專案摘要

ESP32-CAM（AI-Thinker / OV2640 + PSRAM）+ Telegram Bot + 區網 MCP：自適應拍攝、定期監控、**板端 TFLite 人物偵測**。無雲端 API。

## 目前狀態

- 官方 TFLite Micro `person_detection`（~300 KB，96×96 灰階，int8）
- 雙條件門檻：`PERSON_MIN_SCORE=0.70`、`PERSON_SCORE_MARGIN=0.20`
- 人物門檻與警報冷卻預設仍取 config.h；可由 Telegram / MCP 更新並保存 NVS
- 排程：做完再計時；同 loop 只跑一個重型任務（**AI 優先**於 monitor）
- 相機：lazy format（連續 AI 可留灰階；JPEG 需求時才切）
- 曝光／增益／白平衡持續自動；亮度、對比中性，曝光補償 +1（COM3 暗場實拍後調整），增益上限 16x，JPEG quality=8
- JPEG 以 ROM decoder 的 1/8 縮圖分塊計算 RGB 平均值，不配置整張 RGB；切換模式／解析度／設定或亮度明顯改變後，等待亮度與色偏收斂（至少觀察 2 秒，最多約 10 秒，超時回傳最新照片並記錄）
- 穩定場景的連續拍照走短等待；灰階切換／設定更新後先取 20 幀。取像或解碼失敗立即回報，避免累積數十次 framebuffer timeout
- PSRAM JPEG 緩衝依 UXGA 配置；一般仍拍 SVGA，MCP 可選 VGA～UXGA；無 PSRAM 降 CIF
- MCP：板上 `http://esp32cam-<MAC 12 碼>.local/mcp` 或 IP；Streamable HTTP JSON，無 SSE/session
- 多台命名：`device_identity.*` 依出廠 MAC 產生固定 hostname，`config.h` 可選填 `DEVICE_NAME` 顯示名稱；Telegram 開機訊息、`/status`、`camera_status`、Serial 列出連線網址，訊息與照片加設備標籤，Host／Origin 只收本機名稱或 IP
- `MCP_AUTH_TOKEN` 至少 32 字元才開啟；同子網、Host／Origin、輸入大小與時間限制
- 八工具：原 `camera_status`、`camera_capture`、`camera_configure`、`monitor_configure`；新增 `detection_run`、`detection_status`、`detection_configure`、`camera_send_telegram`
- `device_control.*` 共用設定、NVS 單一版本化 blob、拍照、偵測、手動操作冷卻、結果與 health；儲存失敗不改排程，回復相機
- Telegram 新增 `/camera <JSON>`、`/monitor <JSON>`、`/detect`、`/detect_status`、`/detect_config <JSON>`；`/photo [vga|svga|xga|uxga]`，舊指令保留
- 設定包含相機六參數、監控四參數、偵測三參數；含開關自動保存與重開機恢復，未保存時才用 config.h 預設
- 立即偵測只回傳分數、結果與時間，不發警報；排程 AI 使用同一門檻，成功傳送後才起算冷卻，傳送失敗另等 30 秒重試
- health 記錄拍照、推論、Telegram、MCP 圖片與警報的最近嘗試／成功／歷史錯誤；與最近偵測均只保留到重開機，時間是本次開機後秒數
- MCP 與 Telegram 主 loop 串行處理，該輪不再跑其他工作；Telegram 每輪處理一筆佇列，兩入口共用完成後 3 秒操作間隔，每筆 HTTP 後留 100ms
- `platform = espressif32@6.12.0` + `TensorFlowLite_ESP32`；上傳埠 **COM3**

## 架構

```
/photo, /monitor_*  → capturePhoto(JPEG，需要時才切 format) → Telegram
/ai_*               → captureGray96（可停留灰階）→ TFLite Invoke
                    → 達標且 cooldown 未擋 → VGA JPEG 警報
Telegram / POST /mcp → 認證與驗證 → device_control → 相機／偵測／排程／NVS／結果／health
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
| `src/mcp_server.*` | 有界 HTTP、JSON-RPC、工具驗證、分段 base64 圖片 |
| `src/device_control.*` | 兩入口共用設定、NVS 保存、操作、偵測結果及執行紀錄 |
| `src/config.h` | secrets + 門檻（gitignore） |
| `platformio.ini` | platform pin、huge_app、COM3 |
| `mcp.config.example.json` | Agent 連線範本；私有 `mcp.local.json` 含金鑰，gitignore |
| `test/run_host_checks.py` / `test/check_mcp_sdk.py` | 真實 C++ + 硬體替身、官方 MCP SDK 檢查 |

## 建置限制

- **必須** `espressif32@6.12.0`（Arduino 2.x）；新版 platform 54.x 會讓 TFLite 編譯失敗
- Partition：`huge_app.csv`；需 PSRAM 較穩
- 驗證：`pio run` → `pio run -t upload` → Serial 115200 見 `Local person_detection ready`

## 已知限制

- TinyML 準度有限（遠距／暗處／雜物易誤判）
- 警報 JPEG ≠ 模型輸入幀
- `setInsecure()` 不做 TLS 憑證驗證（家用簡化）
- monitor + AI 皆短間隔時仍會忙碌，但不會同 loop 連打兩套
- 固定對焦；極暗環境與失焦仍需實拍處理，不能保證只改參數就更清楚
- 設定保存於 NVS；結果、health 和冷卻時間於重開機清空。開關保存為開啟時，重開機會恢復排程；自動曝光始終開啟，同步工作會讓其他請求等待
- MCP 是可信區網的預共享金鑰 HTTP，未加密、無 OAuth，勿轉發上網
- HTTP header/body 各上限 2048 bytes，讀取期限 1500ms；圖片分段傳輸，失敗會歸還 buffer
- mDNS 名稱 `esp32cam-<MAC>.local`（本機 COM3 板為 `esp32cam-a4f00f75bbe0`）；解析失敗時用 IP。舊 `esp32cam.local` 會被 403 拒絕

## 功能對齊驗證（2026-10-17，八工具版）

- `pio run` 成功；RAM 66020 / 327680 bytes（20.1%）、Flash 1486693 / 3145728 bytes（47.3%），沒有新增韌體第三方依賴；Preferences 為框架內建。
- WSL `python3 test/run_host_checks.py` 通過：實際相機／共用控制／Telegram／MCP 原始碼，包含舊新指令、同批訊息不遺失、非法參數、NVS 首次同值保存／重載／損壞／存檔失敗回復、錯誤保留、偵測失敗不當作無人、警報冷卻與失敗重試、部分／零長度網路寫入及 buffer 歸還。
- `uv run --no-project --with mcp==2.3.0 test/check_mcp_sdk.py` 通過：8 工具全部呼叫、保存、分數、health、Telegram 送圖與完整圖片往返（硬體替身）。
- `pio run -t upload --upload-port COM3` 成功，燒錄 hash 核對通過；firmware.bin SHA-256 `1a719fcd7da8724162882b3726fa31e36ec551e159d383d33d0fcc86c53fdac4`。
- 官方 SDK 2.3.0 直接連實機通過全部 8 工具；單次偵測成功，當時 person=0.117188、no_person=0.882813，整次含取像耗時 12508 ms；最近結果與手動結果一致。
- UXGA 1600×1200 JPEG 為 162432 bytes，Pillow 完整解碼；`camera_send_telegram` 實際送圖成功，Serial 與 health 確認；實機操作後剩餘 heap 169296 bytes。
- COM3 實際硬體重置後，相機、人物門檻、警報冷卻、兩個開關與間隔皆從 NVS 恢復；確認暫存結果清空。再恢復測前設定並第二次重置驗證：monitor/AI 關閉、間隔各 10 秒、曝光 +1、增益 16x、JPEG 8、最低分數 0.7、分數差 0.2、冷卻 0；設定已保存，無 settings_error，剩餘 heap 172152 bytes。
- README 9 個 JSON 區塊、8 工具 schema 與相對連結驗證通過；42 個公開候選檔案未含目前設定的 WiFi 密碼／Telegram Token／MCP Token。私有設定及實機紀錄持續 gitignore。
- 證據：`.pio/parity-build.log`、`parity-host.log`、`parity-sdk.log`、`parity-upload.log`、`parity-live.log`、`parity-live-serial.log`、`parity-reboot.log`、`parity-reboot-serial.log`、`.pio/device-check/parity_*.json`／`parity_uxga.jpg`（皆為私有測試資料）。
- 未驗證：從 Telegram 使用者帳號實際逐條送入所有新指令（路由／回覆以真實 C++ + Bot 替身驗證，實際 Telegram 送圖已驗）；真人進出辨識準度、戶外／實際切換環境光線仍未測。

## 前版驗證（2026-10-17，相機自適應與四工具版）

- `pio run` 成功；保留 platform 6.12.0，沒有新增韌體第三方依賴
- WSL `python3 test/run_host_checks.py`：自動設定、lazy format、錯誤恢復、PSRAM 降級、MCP 認證／來源／輸入／間隔／圖片檢查通過
- `uv run --no-project --with mcp==2.3.0 test/check_mcp_sdk.py`：官方 SDK 握手、四工具、設定、監控及完整圖片往返通過（硬體替身）
- 本機隨機金鑰僅存 gitignore 的 `config.h`／`mcp.local.json`，不寫入文件
- `pio run -t upload --upload-port COM3`：成功，flash hash 核對通過；Serial 見相機與 TFLite ready、WiFi 連線及 setup complete
- 實機 MCP：`http://esp32cam.local/mcp` 正常解析並連線；四工具、錯誤金鑰 401、外來 Host／Origin 403、錯誤版本 400、越界參數拒絕皆通過
- 實拍 VGA／SVGA／XGA／UXGA，Pillow 完整解碼，尺寸皆正確；初版切換解析度的首張偏暗，改為畫面收斂後，最終 UXGA 1600×1200 為 178337 bytes、平均亮度約 146/255
- 同場景曝光補償 0 的 SVGA 平均亮度約 78/255，+1 約 144/255；因此預設改 +1，保留自動曝光、16x 增益上限及 JPEG quality=8
- 初版 JPEG 切換後首張亮度約 76，固定等 2／4 秒仍只有約 95／105；改用實際 RGB 收斂後，首張 143.92、後續 144.85，Serial 顯示約 8.6 秒穩定，偏暗／偏綠問題改善
- Host regression 涵蓋亮度變化、色偏收斂、穩定場景短路、10 秒上限、JPEG 解碼及灰階取像失敗、buffer 歸還；本機與官方 MCP SDK 檢查通過
- Telegram 初次兩張及最終版本照片皆送出成功；測試間短暫出現 TLS 重連錯誤，最終恢復。TFLite 多次推論完成（約 5.6 秒），灰階 lazy 模式及切回 JPEG 都成功；測完監控與 AI 關閉，間隔恢復各 10 秒
- 官方 MCP SDK 2.3.0 直接連實機，握手、四工具、調參數及 JPEG image content 皆通過
- 最終實機以官方 MCP SDK 2.3.0 再次完成握手、四工具、調參數與拍照；重開機後確認曝光補償 +1 已保留
- 證據：`.pio/upload-final-validation.log`、`.pio/hardware-final-serial.log`、`.pio/device-check/final_result.json`、`final_state.json`、`final_after_ai.jpg`、`final_uxga.jpg`（皆屬私有測試資料，不提交）
- 未驗證：實際切換環境光線、明亮戶外場景，以及畫面中真人進出時的人物偵測準度；不把手動曝光補償比較宣稱為明暗環境自適應實測

## Agent 文件

- `AGENTS.md`：canonical 規範；`CLAUDE.md` 僅作入口
- 本檔：交接現況與實作細節
- `README.md`：MCP 啟用、Agent 連線範例、八工具呼叫、Telegram 對照與保存行為與連線排查
