@echo off
rem PDFable Windows build. Double-click this file, or run "./dev.sh windows" in WSL.
rem Everything is logged to build_log.txt. Result: dist\PDFable\PDFable.exe and
rem dist\PDFable-<version>-windows-x64.zip (portable, runs without installation).
setlocal EnableDelayedExpansion
cd /d "%~dp0"
echo Building PDFable... progress is written to build_log.txt
call :main > build_log.txt 2>&1
set RC=%ERRORLEVEL%
if %RC%==0 (echo BUILD OK: dist\PDFable\PDFable.exe) else (echo BUILD FAILED, see build_log.txt)
echo RESULT=%RC%>> build_log.txt
if /i not "%~1"=="nopause" pause
exit /b %RC%

:main
echo === PDFable build %DATE% %TIME%

tasklist /FI "IMAGENAME eq PDFable.exe" 2>nul | find /I "PDFable.exe" >nul && (echo ERROR: PDFable is still running - close all PDFable windows and run build.bat again & exit /b 5)

rem ---- 1. compiler: Visual Studio 2022 or newer
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo ERROR: vswhere.exe not found - Visual Studio Build Tools are not installed & exit /b 10)
set "VSDIR="
for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -products * -version [17.0^,^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR (echo ERROR: no Visual Studio 2022+ with the C++ x64 toolset found. Add the workload "Desktop development with C++". & "%VSWHERE%" -products * -property installationPath & exit /b 11)
echo VS: %VSDIR%
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 (echo ERROR: vcvars64 failed & exit /b 12)
where cl
rem Use the CMake and Ninja that ship with Visual Studio (other copies on PATH, e.g. Strawberry Perl, bring MinGW along).
set "CMAKE=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NINJA=%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%CMAKE%" (echo ERROR: cmake not found - add "C++ CMake tools for Windows" in the Visual Studio Installer & exit /b 13)
if not exist "%NINJA%" (echo ERROR: ninja not found - add "C++ CMake tools for Windows" in the Visual Studio Installer & exit /b 14)
"%CMAKE%" --version

rem ---- 2. Qt 6 MSVC kit
set "QTDIR="
for %%r in ("C:\Qt" "%USERPROFILE%\Qt" "D:\Qt" "%~dp0deps\Qt") do (
  if exist "%%~r" for /d %%v in ("%%~r\6.*") do (
    if exist "%%v\msvc2022_64\bin\windeployqt.exe" set "QTDIR=%%v\msvc2022_64"
  )
)
if not defined QTDIR (
  echo Qt 6 not found - downloading the MSVC 2022 kit into deps\Qt with aqtinstall
  set "PY=python"
  where python >nul 2>nul || set "PY=%USERPROFILE%\anaconda3\python.exe"
  "!PY!" --version || (echo ERROR: no Python found to run aqtinstall. Install Qt 6 ^(MSVC 2022 64-bit^) to C:\Qt and run again. & exit /b 20)
  "!PY!" -m pip install --quiet aqtinstall || exit /b 21
  "!PY!" -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 --archives qtbase -O "%~dp0deps\Qt" || exit /b 22
  set "QTDIR=%~dp0deps\Qt\6.8.3\msvc2022_64"
)
echo Qt: %QTDIR%
if not exist "%QTDIR%\bin\windeployqt.exe" (echo ERROR: Qt kit incomplete & exit /b 23)

rem ---- 3. vcpkg (MuPDF)
set "VCPKG=%VCPKG_ROOT%"
if not exist "%VCPKG%\scripts\buildsystems\vcpkg.cmake" set "VCPKG=C:\vcpkg"
if not exist "%VCPKG%\scripts\buildsystems\vcpkg.cmake" (echo ERROR: vcpkg not found at C:\vcpkg & exit /b 30)
if not exist "%VCPKG%\ports\libmupdf" (echo vcpkg checkout is old, updating & git -C "%VCPKG%" pull --ff-only)
if not exist "%VCPKG%\ports\libmupdf" (echo ERROR: vcpkg has no libmupdf port & exit /b 31)
if not exist "%VCPKG%\vcpkg.exe" (call "%VCPKG%\bootstrap-vcpkg.bat" -disableMetrics || exit /b 32)
echo vcpkg: %VCPKG%
findstr /c:"\"version\"" "%VCPKG%\ports\libmupdf\vcpkg.json"

rem ---- 4. configure + build
"%CMAKE%" -S . -B build -G Ninja "-DCMAKE_MAKE_PROGRAM=%NINJA%" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%QTDIR%" "-DCMAKE_TOOLCHAIN_FILE=%VCPKG%\scripts\buildsystems\vcpkg.cmake" -DVCPKG_TARGET_TRIPLET=x64-windows-static-md
if errorlevel 1 (echo ERROR: configure failed & exit /b 40)
"%CMAKE%" --build build
if errorlevel 1 (echo ERROR: compile failed & exit /b 41)
rem ---- 5. portable folder: dist\PDFable with Qt, plugins and the VC++ runtime next to the exe
set "STAGE=%~dp0dist\PDFable"
if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%STAGE%" (echo ERROR: cannot clear dist\PDFable - is PDFable running from there? & exit /b 43)
"%CMAKE%" --install build --prefix "%STAGE%"
if errorlevel 1 (echo ERROR: install step failed & exit /b 44)
"%QTDIR%\bin\windeployqt.exe" --release --no-translations --no-compiler-runtime --no-opengl-sw --no-system-d3d-compiler --skip-plugin-types tls,networkinformation "%STAGE%\PDFable.exe"
if errorlevel 1 (echo ERROR: windeployqt failed & exit /b 42)
set "CRT="
for /d %%d in ("%VCToolsRedistDir%x64\Microsoft.VC14*.CRT") do set "CRT=%%d"
if not defined CRT (echo ERROR: VC++ runtime DLLs not found under "%VCToolsRedistDir%" & exit /b 45)
copy /y "%CRT%\*.dll" "%STAGE%\" >nul
echo VC++ runtime: %CRT%

rem ---- 6. zip
set "VER="
for /f "tokens=2 delims==" %%v in ('findstr /b /c:"CMAKE_PROJECT_VERSION:STATIC" build\CMakeCache.txt') do set "VER=%%v"
set "ZIP=%~dp0dist\PDFable-%VER%-windows-x64.zip"
if exist "%ZIP%" del /q "%ZIP%"
powershell -NoProfile -Command "Compress-Archive -Path '%STAGE%' -DestinationPath '%ZIP%' -CompressionLevel Optimal"
if errorlevel 1 (echo ERROR: could not create the zip & exit /b 46)
echo Portable package: %ZIP%
echo === BUILD OK
exit /b 0
