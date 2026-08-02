@echo off
rem OpenRA2 launcher: always starts the latest Release build; run from project root so assets/ is found.
cd /d "%~dp0"
start "" "%~dp0build\Release\ra2.exe"
