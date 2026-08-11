@echo off
REM Regression: user content drop-in (run from repo root)
setlocal
set EXE=build\Release\ra2.exe
if not exist "%EXE%" (
  echo FAIL: missing %EXE% — build Release first
  exit /b 1
)

echo === baseline ===
"%EXE%" --content-check expect-missions 70
if errorlevel 1 exit /b 1

echo === csv patch ===
copy /y userdata\content\rules\units.csv userdata\content\rules\units.csv.bak >nul
>userdata\content\rules\units.csv echo Name,Base,HP,Cost,Speed,Sight,WeaponDamage,WeaponRange,Buildable,DisplayName
>>userdata\content\rules\units.csv echo Grizzly,,777,888,,,,,,TestGrizzly
"%EXE%" --content-check expect-grizzly-cost 888 expect-grizzly-hp 777
set ERR=%ERRORLEVEL%
copy /y userdata\content\rules\units.csv.bak userdata\content\rules\units.csv >nul
del userdata\content\rules\units.csv.bak >nul 2>&1
if not "%ERR%"=="0" exit /b 1

echo === ExampleCustom mod ===
"%EXE%" --mod mods/ExampleCustom --content-check expect-missions 71 expect-variant HeavyGrizzly expect-map maps/example_demo.txt
if errorlevel 1 exit /b 1

echo ALL USER-CONTENT CHECKS PASSED
exit /b 0
