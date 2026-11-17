@echo off
rem MixLauncher build (Rust, cargo; needs the stable toolchain for x86_64-pc-windows-msvc).
rem   build.bat           release build -> build\mixlauncher.exe (runs as administrator)
rem   build.bat debug     debug build with symbols (runs as administrator)
rem   build.bat dev       release code without the elevation prompt -> build\mixlauncher-dev.exe
rem The published builds are profile-guided and made per processor level: release.bat (pgo.bat).
setlocal
cd /d "%~dp0"
if not exist build mkdir build

set MODE=release
set OUT=mixlauncher
set CARGO=cargo build --release
set SRC=target\x86_64-pc-windows-msvc\release\mixlauncher.exe
if /i "%1"=="debug" (
    set MODE=debug
    set CARGO=cargo build
    set SRC=target\x86_64-pc-windows-msvc\debug\mixlauncher.exe
)
if /i "%1"=="dev" (
    set OUT=mixlauncher-dev
    set CARGO=cargo build --release --features dev --target-dir target\dev
    set SRC=target\dev\x86_64-pc-windows-msvc\release\mixlauncher.exe
)

where cargo >nul 2>nul || (
    echo cargo not found. Install Rust from https://rustup.rs
    exit /b 1
)
echo [cargo %MODE% %OUT%]
%CARGO% || exit /b 1

rem Ask a running instance of this exe to exit so it can be overwritten. This does not start the
rem exe (that would ask for elevation): it posts a message to its window.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\stop-running.ps1" -name %OUT% >nul 2>nul
copy /y "%SRC%" build\%OUT%.exe >nul || exit /b 1
for %%f in (build\%OUT%.exe) do echo build\%OUT%.exe  %%~zf bytes

rem Built-in Everything next to the exe, as the installer lays it out (tools\fetch-everything.ps1).
if exist third_party\everything\Everything.exe (
    if not exist build\Everything mkdir build\Everything
    xcopy /d /y /q third_party\everything\Everything.exe build\Everything\ >nul 2>nul
    xcopy /d /y /q third_party\everything\License.txt build\Everything\ >nul 2>nul
)
endlocal
