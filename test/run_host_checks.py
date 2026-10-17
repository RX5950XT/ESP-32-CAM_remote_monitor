"""Build/run camera, shared controls, Telegram and MCP with deterministic hardware doubles."""
from pathlib import Path
import shutil
import subprocess

root = Path(__file__).resolve().parents[1]
compiler = shutil.which("g++")
if not compiler:
    raise SystemExit("g++ is required for host checks")
output = root / ".pio" / "host-checks.exe"
output.parent.mkdir(exist_ok=True)
subprocess.run([
    compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-Wno-format", "-Wno-sign-compare",
    "-I" + str(root / "test" / "host"),
    "-I" + str(root / ".pio" / "libdeps" / "esp32cam" / "ArduinoJson" / "src"),
    str(root / "test" / "host" / "test_firmware.cpp"), "-o", str(output),
], check=True, cwd=root)
subprocess.run([str(output)], check=True, cwd=root)
