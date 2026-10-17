"""Official Python MCP client -> HTTP -> actual firmware C++ with hardware doubles.

Run host checks first, then: uv run --no-project --with mcp==2.3.0 test/check_mcp_sdk.py
This is a protocol test; it does not test a physical sensor or WiFi.
"""
import asyncio
import base64
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
from pathlib import Path
import subprocess
import sys
import threading

import httpx2 as httpx
from mcp import ClientSession
from mcp.client.streamable_http import streamable_http_client

ROOT = Path(__file__).resolve().parents[1]
TOKEN = "host-test-token-not-a-secret-123456"
DEVICE_ID = "246f28a1b2c3"
HOSTNAME = f"esp32cam-{DEVICE_ID}"
exe = ROOT / ".pio" / "host-checks.exe"
command = [str(exe), "--bridge"]
if sys.platform == "win32":
    linux_path = subprocess.check_output(["wsl", "-d", "Ubuntu-24.04", "--exec", "wslpath", "-u", str(exe)], text=True).strip()
    command = ["wsl", "-d", "Ubuntu-24.04", "--exec", linux_path, "--bridge"]


async def check(url):
    async with httpx.AsyncClient(headers={"Authorization": f"Bearer {TOKEN}"}, timeout=30) as http:
        async with streamable_http_client(url, http_client=http) as (read, write):
            async with ClientSession(read, write) as session:
                result = await session.initialize()
                assert result.server_info.name == HOSTNAME
                assert result.server_info.version == "1.1.0"
                tools = await session.list_tools()
                assert {t.name for t in tools.tools} == {
                    "camera_status", "camera_capture", "camera_configure", "monitor_configure",
                    "detection_run", "detection_status", "detection_configure", "camera_send_telegram",
                }
                status = await session.call_tool("camera_status", {})
                assert not status.is_error and status.structured_content["automatic_exposure"]
                assert status.structured_content["free_heap_bytes"] == 123456
                assert status.structured_content["device_id"] == DEVICE_ID
                assert status.structured_content["device_name"] == HOSTNAME
                assert status.structured_content["hostname"] == f"{HOSTNAME}.local"
                assert status.structured_content["mcp_enabled"] and status.structured_content["mdns_ready"]
                assert status.structured_content["mcp_url"] == f"http://{HOSTNAME}.local/mcp"
                assert not status.structured_content["detection"]["available"]
                changed = await session.call_tool("camera_configure", {"exposure_level": -1})
                assert not changed.is_error and changed.structured_content["exposure_level"] == -1
                monitor = await session.call_tool("monitor_configure", {"monitor_enabled": True, "monitor_interval_sec": 30})
                assert not monitor.is_error and monitor.structured_content["monitor_enabled"]
                assert monitor.structured_content["settings_persisted"]
                config = await session.call_tool("detection_configure", {
                    "person_min_score": 0.8, "person_score_margin": 0.3, "alert_cooldown_sec": 45,
                })
                assert not config.is_error and config.structured_content["alert_cooldown_sec"] == 45
                detected = await session.call_tool("detection_run", {})
                assert not detected.is_error and detected.structured_content["success"]
                assert detected.structured_content["person_detected"]
                assert detected.structured_content["device_id"] == DEVICE_ID
                recent = await session.call_tool("detection_status", {})
                assert not recent.is_error and recent.structured_content == detected.structured_content
                photo = await session.call_tool("camera_capture", {"resolution": "uxga"})
                assert not photo.is_error and photo.content[0].type == "image"
                assert photo.structured_content["device_id"] == DEVICE_ID
                assert photo.structured_content["device_name"] == HOSTNAME
                assert base64.b64decode(photo.content[0].data) == bytes(i % 256 for i in range(1541))
                delivery = await session.call_tool("camera_send_telegram", {"resolution": "vga"})
                assert not delivery.is_error and delivery.structured_content["sent_to_telegram"]
                assert delivery.structured_content["device_id"] == DEVICE_ID
                delivered_status = await session.call_tool("camera_status", {})
                assert delivered_status.structured_content["health"]["telegram"]["success"]
                off = await session.call_tool("monitor_configure", {"monitor_enabled": False})
                assert not off.is_error and not off.structured_content["monitor_enabled"]
                print("Official MCP SDK: initialize, all 8 tools, persisted settings, scores, health, Telegram delivery and full image round-trip passed")


def main():
    bridge = subprocess.Popen(command, stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, encoding="utf-8")
    lock = threading.Lock()

    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def handle_request(self):
            body = self.rfile.read(int(self.headers.get("Content-Length", 0))).decode()
            headers = "".join(f"{key}: {value}\r\n" for key, value in self.headers.items() if key.lower() != "host")
            raw = f"{self.command} /mcp HTTP/1.1\r\nHost: {HOSTNAME}.local\r\n{headers}\r\n{body}"
            with lock:
                bridge.stdin.write(json.dumps({"request": raw}) + "\n")
                bridge.stdin.flush()
                response = json.loads(bridge.stdout.readline())["response"]
            self.connection.sendall(response.encode())
            self.close_connection = True

        do_POST = do_GET = do_DELETE = handle_request

        def log_message(self, *_):
            pass

    server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    try:
        asyncio.run(check(f"http://127.0.0.1:{server.server_port}/mcp"))
    finally:
        server.shutdown()
        server.server_close()
        bridge.stdin.close()
        bridge.wait(timeout=10)


if __name__ == "__main__":
    main()
