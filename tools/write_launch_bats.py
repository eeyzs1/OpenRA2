# -*- coding: utf-8 -*-
"""Rewrite launch.bat / 启动游戏.bat as ASCII + CRLF + no BOM."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# PowerShell Start-Process detaches cleanly (bat `start` + CUI used to die on CTRL_CLOSE).
LAUNCH = r"""@echo off
setlocal EnableExtensions
rem OpenRA2 launcher: ASCII + CRLF + no BOM (required for Explorer double-click).
cd /d "%~dp0" || (
  echo [OpenRA2] cannot cd to script directory
  pause
  exit /b 1
)

set "LOG=%CD%\launch_log.txt"
> "%LOG%" echo [%date% %time%] OpenRA2 launcher
>> "%LOG%" echo CD=%CD%

set "EXE="
if exist "%CD%\build\Release\ra2.exe" set "EXE=%CD%\build\Release\ra2.exe"
if not defined EXE if exist "%CD%\build-asan\Debug\ra2.exe" set "EXE=%CD%\build-asan\Debug\ra2.exe"
if not defined EXE if exist "%CD%\build\Debug\ra2.exe" set "EXE=%CD%\build\Debug\ra2.exe"

if not defined EXE (
  >> "%LOG%" echo FAIL: ra2.exe not found
  echo [OpenRA2] ra2.exe not found. Build first:
  echo   cmake --build build --config Release --target ra2
  pause
  exit /b 1
)

>> "%LOG%" echo EXE=%EXE%
echo [OpenRA2] launching:
echo   %EXE%

rem Absolute path + WorkingDirectory; verify process still alive after 2s.
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "Start-Process -FilePath '%EXE%' -WorkingDirectory '%CD%'; Start-Sleep -Seconds 2; if (-not (Get-Process -Name 'ra2' -ErrorAction SilentlyContinue)) { exit 2 } else { exit 0 }"
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" (
  >> "%LOG%" echo FAIL: ra2 exited immediately rc=%RC%
  echo [OpenRA2] Game exited immediately after launch.
  echo See launch_log.txt
  pause
  exit /b 1
)
>> "%LOG%" echo start_ok
exit /b 0
"""

WRAPPER = r"""@echo off
call "%~dp0launch.bat"
"""


def to_crlf_ascii(text: str) -> bytes:
    text = text.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")
    raw = text.encode("ascii")
    if raw.startswith(b"\xef\xbb\xbf"):
        raise SystemExit("refusing to write UTF-8 BOM")
    return raw


def main() -> None:
    (ROOT / "launch.bat").write_bytes(to_crlf_ascii(LAUNCH))
    (ROOT / "启动游戏.bat").write_bytes(to_crlf_ascii(WRAPPER))
    print("wrote launch.bat and 启动游戏.bat (ASCII CRLF no BOM)")


if __name__ == "__main__":
    main()
