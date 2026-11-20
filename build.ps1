# Builds PDFable on Windows. Run from a "x64 Native Tools Command Prompt for VS 2022" / Developer PowerShell.
#   .\build.ps1 -QtDir C:\Qt\6.8.3\msvc2022_64 -Vcpkg C:\vcpkg
param(
    [Parameter(Mandatory = $true)][string]$QtDir,   # Qt 6 MSVC kit, e.g. C:\Qt\6.8.3\msvc2022_64
    [string]$Vcpkg = $env:VCPKG_ROOT,               # vcpkg checkout (provides MuPDF)
    [switch]$Tests
)
$ErrorActionPreference = "Stop"
if (-not $Vcpkg) { throw "Pass -Vcpkg <path> or set VCPKG_ROOT" }
$testFlag = if ($Tests) { "ON" } else { "OFF" }
cmake -S $PSScriptRoot -B "$PSScriptRoot\build" -G Ninja `
    -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_PREFIX_PATH="$QtDir" `
    -DCMAKE_TOOLCHAIN_FILE="$Vcpkg\scripts\buildsystems\vcpkg.cmake" `
    -DVCPKG_TARGET_TRIPLET=x64-windows-static-md `
    -DPDFABLE_TESTS=$testFlag
cmake --build "$PSScriptRoot\build"
& "$QtDir\bin\windeployqt.exe" --release --no-translations "$PSScriptRoot\build\PDFable.exe"
Write-Host "Done: $PSScriptRoot\build\PDFable.exe"
