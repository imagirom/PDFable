@echo off
rem Runs dev.sh inside WSL (Ubuntu) from Windows, e.g.  wsl-dev.bat setup all
rem Double-click without arguments = "setup all" (setup is quick once everything is installed).
rem "setup" first installs the Linux packages as root, so no sudo password is needed.
rem Logs: logs\dev-<command>.log, overall result in logs\wsl-dev.result
setlocal EnableDelayedExpansion
cd /d "%~dp0"
if not exist logs mkdir logs
set "HERE=%~dp0."
set RC=0
set "ARGS=%*"
if "%~1"=="" set "ARGS=setup all"
for %%c in (%ARGS%) do (
  if !RC!==0 if /i "%%c"=="setup" (
    echo === installing Linux packages in WSL ^(as root^)
    wsl.exe -u root --cd "%HERE%" -- bash ./dev.sh packages
    if errorlevel 1 set RC=1
  )
  if !RC!==0 (
    echo === dev.sh %%c
    wsl.exe --cd "%HERE%" -- bash ./dev.sh %%c
    if errorlevel 1 set RC=1
  )
)
echo.
if %RC%==0 (echo === finished) else (echo === FAILED - see the logs folder)
echo %RC%> logs\wsl-dev.result
if /i not "%PDFABLE_NOPAUSE%"=="1" pause
exit /b %RC%
