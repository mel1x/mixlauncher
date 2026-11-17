@echo off
rem MixLauncher release: three profile-guided builds (pgo.bat) - generic x64, x86-64-v2 (SSE4.2), x86-64-v3 (AVX2):
rem   build\MixLauncher-Setup.exe        all three inside, the installer picks one by the processor
rem   build\MixLauncher-portable.exe     any x64
rem   build\MixLauncher-portable-v2.exe  SSE4.2 (2009+)
rem   build\MixLauncher-portable-v3.exe  AVX2 (Intel 2013+, AMD 2015+)
rem A running build\mixlauncher.exe is not touched. Needs Inno Setup 7 (https://jrsoftware.org/isdl.php) and
rem "rustup component add llvm-tools". The version is in res\version.h.
setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch-everything.ps1 || exit /b 1

set "ISCC="
for /f "delims=" %%i in ('where iscc 2^>nul') do if not defined ISCC set "ISCC=%%i"
for %%p in ("%LOCALAPPDATA%\Programs\Inno Setup 7\ISCC.exe" "%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe" "%ProgramFiles%\Inno Setup 7\ISCC.exe") do (
    if not defined ISCC if exist %%p set "ISCC=%%~p"
)
if not defined ISCC (
    echo Inno Setup 7 not found. Install it from https://jrsoftware.org/isdl.php
    exit /b 1
)

call "%~dp0pgo.bat" || exit /b 1
call "%~dp0pgo.bat" v2 || exit /b 1
call "%~dp0pgo.bat" v3 || exit /b 1
set "GEN=target\pgo\x86_64-pc-windows-msvc\release\mixlauncher.exe"
set "V2=target\pgo-v2\x86_64-pc-windows-msvc\release\mixlauncher.exe"
set "V3=target\pgo-v3\x86_64-pc-windows-msvc\release\mixlauncher.exe"
if not exist build mkdir build
copy /y "%GEN%" build\MixLauncher-portable.exe >nul || exit /b 1
copy /y "%V2%" build\MixLauncher-portable-v2.exe >nul || exit /b 1
copy /y "%V3%" build\MixLauncher-portable-v3.exe >nul || exit /b 1
"%ISCC%" /Q "/DSourceExe=..\%GEN%" "/DSourceExeV2=..\%V2%" "/DSourceExeV3=..\%V3%" installer\mixlauncher.iss || exit /b 1
for %%f in (build\MixLauncher-portable.exe build\MixLauncher-portable-v2.exe build\MixLauncher-portable-v3.exe build\MixLauncher-Setup.exe) do echo %%f  %%~zf bytes
endlocal
