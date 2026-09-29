@echo off
rem MixLauncher build: one translation unit, one compiler call.
rem   build.bat           release build -> build\mixlauncher.exe (runs as administrator)
rem   build.bat debug     debug build with symbols (runs as administrator)
rem   build.bat dev       release code without the elevation prompt -> build\mixlauncher-dev.exe
rem Uses MSVC (cl) when available (run from a "x64 Native Tools" prompt or it is found via
rem vswhere), otherwise MinGW-w64 gcc/clang (MSYS2 ucrt64/clang64 are picked up automatically).
setlocal
cd /d "%~dp0"
if not exist build mkdir build

set MODE=release
set OUT=mixlauncher
set RCDEF=
if /i "%1"=="debug" set MODE=debug
if /i "%1"=="dev" (
    set OUT=mixlauncher-dev
    set RCDEF=ML_DEV_BUILD
)

rem Ask a running instance of this exe to exit so it can be overwritten. This does not start the
rem exe (that would ask for elevation): it posts a message to its window.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\stop-running.ps1" -name %OUT% >nul 2>nul

where cl >nul 2>nul && goto :msvc

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
    for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        if exist "%%i\VC\Auxiliary\Build\vcvars64.bat" (
            call "%%i\VC\Auxiliary\Build\vcvars64.bat" >nul
            goto :msvc
        )
    )
)

where gcc >nul 2>nul && goto :gcc
where clang >nul 2>nul && goto :clang
if exist C:\msys64\ucrt64\bin\gcc.exe (
    set "PATH=C:\msys64\ucrt64\bin;%PATH%"
    goto :gcc
)
if exist C:\msys64\clang64\bin\clang.exe (
    set "PATH=C:\msys64\clang64\bin;%PATH%"
    goto :clang
)
echo No C compiler found. Install Visual Studio Build Tools or MSYS2 (pacman -S mingw-w64-ucrt-x86_64-gcc).
exit /b 1

:msvc
echo [msvc %MODE% %OUT%]
set RCFLAGS=/nologo
if defined RCDEF set RCFLAGS=/nologo /d %RCDEF%
rc %RCFLAGS% /fo build\%OUT%.res res\mixlauncher.rc || exit /b 1
set CFLAGS=/nologo /utf-8 /W3 /std:c11 /GS- /Gw /Gy /MT /DNDEBUG
if "%MODE%"=="debug" (set CFLAGS=/nologo /utf-8 /W3 /std:c11 /Zi /Od /MTd)
cl %CFLAGS% /O2 src\main.c build\%OUT%.res /Fe:build\%OUT%.exe /Fo:build\ /Fd:build\ /link /SUBSYSTEM:WINDOWS /OPT:REF /OPT:ICF /INCREMENTAL:NO || exit /b 1
goto :done

:gcc
set CC=gcc
goto :gnu
:clang
set CC=clang
:gnu
echo [%CC% %MODE% %OUT%]
set RCFLAGS=
if defined RCDEF set RCFLAGS=-D%RCDEF%
windres %RCFLAGS% -I res res\mixlauncher.rc -O coff -o build\%OUT%.res.o || exit /b 1
rem (no -fdata-sections: MinGW's PE linker would move .bss into .data and bloat the exe)
set CFLAGS=-std=c11 -O2 -s -DNDEBUG
if "%MODE%"=="debug" set CFLAGS=-std=c11 -O0 -g
%CC% %CFLAGS% -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers -Wno-cast-function-type -mwindows ^
    src\main.c build\%OUT%.res.o -o build\%OUT%.exe -static ^
    -ld3d11 -ldxgi -ldwmapi -lole32 -loleaut32 -luuid -lshell32 -lshlwapi -luser32 -lgdi32 -ladvapi32 || exit /b 1

:done
for %%f in (build\%OUT%.exe) do echo build\%OUT%.exe  %%~zf bytes
endlocal
