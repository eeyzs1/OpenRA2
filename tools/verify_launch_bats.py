# -*- coding: utf-8 -*-
"""Fail if launch bats are BOM / LF-only / dangerous start /D form."""
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]


def check(path: Path, *, must_call_launch: bool = False) -> list[str]:
    errs: list[str] = []
    if not path.is_file():
        return [f"missing: {path.name}"]
    data = path.read_bytes()
    if data.startswith(b"\xef\xbb\xbf"):
        errs.append(f"{path.name}: UTF-8 BOM (cmd double-click breaks)")
    if data.startswith(b"\xff\xfe") or data.startswith(b"\xfe\xff"):
        errs.append(f"{path.name}: UTF-16 BOM")
    if b"\r\n" not in data:
        errs.append(f"{path.name}: no CRLF line endings")
    # Lone LF (Unix) — strip CRLF then look for remaining LF
    if b"\n" in data.replace(b"\r\n", b""):
        errs.append(f"{path.name}: LF without CR (must be CRLF-only)")
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError:
        errs.append(f"{path.name}: non-ASCII content (keep body ASCII)")
        return errs
    # Strip rem comments before pattern checks (docs may mention forbidden forms).
    code_lines = []
    for ln in text.splitlines():
        s = ln.strip()
        if s.lower().startswith("rem ") or s.lower() == "rem":
            continue
        code_lines.append(ln)
    code = "\n".join(code_lines)
    if must_call_launch:
        if "launch.bat" not in code:
            errs.append(f"{path.name}: must call launch.bat")
        return errs
    if 'cd /d "%~dp0"' not in code:
        errs.append(f'{path.name}: missing cd /d "%~dp0"')
    if '/D "%~dp0"' in code:
        errs.append(f'{path.name}: forbidden start /D "%~dp0" (trailing \\ quote bug)')
    if "Start-Process" not in code:
        errs.append(f"{path.name}: must launch via PowerShell Start-Process")
    if r"build\Release\ra2.exe" not in code:
        errs.append(f"{path.name}: missing Release ra2.exe path")
    return errs


def main() -> int:
    errs = []
    errs += check(ROOT / "launch.bat")
    errs += check(ROOT / "启动游戏.bat", must_call_launch=True)
    if errs:
        print("VERIFY LAUNCH BATS: FAIL")
        for e in errs:
            print(" -", e)
        return 1
    print("VERIFY LAUNCH BATS: PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
