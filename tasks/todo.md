# Telegram / MCP 功能對齊

## 多台設備命名

- [x] 依完整 MAC 產生固定 hostname，支援選填顯示名稱；Host／Origin 驗證同步改用該設備名稱。
- [x] Telegram 開機、狀態與所有照片／訊息標示設備，MCP 提供身分與連線資訊。
- [x] mDNS 公告服務，更新多台連線範本及名稱查詢說明；保留私有金鑰與 NVS 設定。
- [x] Host／官方 SDK 回歸、COM3 燒錄及新名稱／重開機穩定性驗證。

- [x] 共用設定與 NVS 保存：相機、監控、人物門檻、警報冷卻，失敗不可回報成功。
- [x] 共用立即偵測、最近結果、拍照送 Telegram、健康紀錄與操作冷卻。
- [x] Telegram 舊指令保留，新增畫質／JSON 設定、解析度、偵測結果；MCP 保留原四工具並補齊。
- [x] 回歸測試覆蓋兩入口、非法輸入、保存／載入／儲存失敗、推論失敗與排程。
- [x] 編譯、官方 MCP SDK、COM3 燒錄與實機驗證；更新 README / CONTEXT。

## 驗收

- 調參數與狀態在兩入口一致，重開機保留；真人辨識準度不以協定測試代替。
- 測完監控與 AI 關閉，間隔與門檻恢復原設定；保留所有私有金鑰。

## Review（2026-10-17）

- 六項缺口已雙入口對齊，共用操作與設定；MCP 8 工具，Telegram 舊指令保留。
- build、host checks、官方 SDK、COM3 燒錄與實機 SDK／偵測／JPEG／Telegram 傳圖／兩次重開機保存均通過，詳見 CONTEXT。
- 唯讀獨立審閱未發現阻擋問題；SDK 回傳欄位測試已修正，補齊同批 Telegram 訊息不遺失的回歸。
- 測完已恢復原設定並保存、重開機確認。未從真人 Telegram 帳號逐條送入新指令；該流程用真實 router 與 Bot 替身驗證。真人辨識準度與明暗環境切換未測。

## Review（多台命名，2026-10-17）

- host checks、官方 SDK、`pio run`、COM3 燒錄通過；實機 hostname `esp32cam-a4f00f75bbe0.local` 可解析，SDK 8 工具與身分欄位正確，送 Telegram 照片成功。
- 舊 `esp32cam.local` 與他台名稱的 Host 皆 403，IP 200；重開機後名稱與關閉中的監控／AI 設定不變。私有 `mcp.local.json` 已改用新網址。
