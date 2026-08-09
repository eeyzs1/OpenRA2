# -*- coding: utf-8 -*-
"""Rewrite launch.bat / startup wrapper bat as ASCII + CRLF + no BOM."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# Single canonical exe: cmake --build build --config Release --target ra2
# -> build\Release\ra2.exe. Do not fall back to Debug/root copies (stale).
# PowerShell Start-Process detaches cleanly (bat `start` + CUI used to die on CTRL_CLOSE).
LAUNCH = r"""@echo off
setlocal EnableExtensions
rem OpenRA2 launcher: ASCII + CRLF + no BOM (required for Explorer double-click).
rem Always runs build\Release\ra2.exe (the Release cmake output). Rebuild updates that file.
cd /d "%~dp0" || (
  echo [OpenRA2] cannot cd to script directory
  pause
  exit /b 1
)

set "LOG=%CD%\launch_log.txt"
> "%LOG%" echo [%date% %time%] OpenRA2 launcher
>> "%LOG%" echo CD=%CD%

set "EXE=%CD%\build\Release\ra2.exe"
if not exist "%EXE%" (
  >> "%LOG%" echo FAIL: missing %EXE%
  echo [OpenRA2] build\Release\ra2.exe not found.
  echo Build first:
  echo   cmake --build build --config Release --target ra2
  echo Then double-click this bat again.
  pause
  exit /b 1
)

>> "%LOG%" echo EXE=%EXE%
for %%I in ("%EXE%") do >> "%LOG%" echo EXE_TIME=%%~tI SIZE=%%~zI
echo [OpenRA2] launching Release build:
echo   %EXE%
for %%I in ("%EXE%") do echo   stamped %%~tI

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
    wrapper_path = ROOT / "\u542f\u52a8\u6e38\u620f.bat"  # 启动游戏.bat
    wrapper_path.write_bytes(to_crlf_ascii(WRAPPER))
    print("wrote launch.bat and startup wrapper (ASCII CRLF no BOM)")
    print("canonical exe: build\\Release\\ra2.exe only")


if __name__ == "__main__":
    main()
