@echo off
rem MixLauncher release: build\mixlauncher.exe + built-in Everything -> build\MixLauncher-Setup.exe
rem Needs Inno Setup 7 (https://jrsoftware.org/isdl.php). The version is ProductVersion in res\mixlauncher.rc.
rem Building closes a running build\mixlauncher.exe (see build.bat).
setlocal
cd /d "%~dp0"

powershell -NoProfile -ExecutionPolicy Bypass -File tools\fetch-everything.ps1 || exit /b 1
call build.bat || exit /b 1
if not exist build\mixlauncher.exe exit /b 1

set "ISCC="
for /f "delims=" %%i in ('where iscc 2^>nul') do if not defined ISCC set "ISCC=%%i"
for %%p in ("%LOCALAPPDATA%\Programs\Inno Setup 7\ISCC.exe" "%ProgramFiles(x86)%\Inno Setup 7\ISCC.exe" "%ProgramFiles%\Inno Setup 7\ISCC.exe") do (
    if not defined ISCC if exist %%p set "ISCC=%%~p"
)
if not defined ISCC (
    echo Inno Setup 7 not found. Install it from https://jrsoftware.org/isdl.php
    exit /b 1
)
"%ISCC%" /Q installer\mixlauncher.iss || exit /b 1
for %%f in (build\MixLauncher-Setup.exe) do echo build\MixLauncher-Setup.exe  %%~zf bytes
endlocal
