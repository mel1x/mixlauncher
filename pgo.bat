@echo off
rem Profile-guided release build.
rem   pgo.bat         -> target\pgo\x86_64-pc-windows-msvc\release\mixlauncher.exe   (any x64 CPU)
rem   pgo.bat v2|v3|v4 -> target\pgo-vN\... built for x86-64-vN (v2: SSE4.2, v3: AVX2, v4: AVX-512)
rem   pgo.bat ... dev  -> the same with the dev manifest (no elevation prompt)
rem 1. an instrumented build with the same features as the final one (a feature changes the symbol names and the
rem    profile would not match); the training runs it as the invoker, without the elevation prompt,
rem 2. a training run on this machine's data: --bench (cache, search, frames), --dump-apps, --test-everything, --media-dump,
rem 3. the profiles merged (llvm-profdata from "rustup component add llvm-tools"),
rem 4. the release build optimized with them.
setlocal enabledelayedexpansion
cd /d "%~dp0"

set CPU=
set NAME=pgo
set FEAT=
for %%a in (%*) do (
    for %%l in (v2 v3 v4) do if /i "%%a"=="%%l" (
        set CPU=-C target-cpu=x86-64-%%l
        set NAME=pgo-%%l
    )
    if /i "%%a"=="dev" set FEAT=--features dev
)
set BASEFLAGS=-C target-feature=+crt-static %CPU%
set PROF=%CD%\target\%NAME%-profiles

for /f "delims=" %%i in ('rustc --print sysroot') do set "SYSROOT=%%i"
set "PROFDATA=%SYSROOT%\lib\rustlib\x86_64-pc-windows-msvc\bin\llvm-profdata.exe"
if not exist "%PROFDATA%" (
    echo llvm-profdata not found: rustup component add llvm-tools
    exit /b 1
)

if exist "%PROF%" rmdir /s /q "%PROF%"
mkdir "%PROF%"

echo [1/4] instrumented build
set "RUSTFLAGS=%BASEFLAGS% -C profile-generate=%PROF%"
cargo build --release %FEAT% --target-dir target\%NAME%-gen || exit /b 1
set "GEN=target\%NAME%-gen\x86_64-pc-windows-msvc\release\mixlauncher.exe"

echo [2/4] training
set "OUT=%CD%\target\%NAME%-train"
if exist "%OUT%" rmdir /s /q "%OUT%"
mkdir "%OUT%"
rem one file per run (%p: the process id)
set "LLVM_PROFILE_FILE=%PROF%\ml-%%p.profraw"
set __COMPAT_LAYER=RunAsInvoker
"%GEN%" --bench 300 "%OUT%\bench.txt" || exit /b 1
"%GEN%" --dump-apps "%OUT%\apps.txt"
for %%q in (main.c chrome readme ghbdtn) do "%GEN%" --test-everything %%q "%OUT%\ev.txt"
"%GEN%" --media-dump "%OUT%\md.bmp" 96
"%GEN%" --media-dump "%OUT%\md.bmp" 144 light
"%GEN%" --write-icon "%OUT%\icon.ico"

echo [3/4] merging profiles
set LLVM_PROFILE_FILE=
set __COMPAT_LAYER=
"%PROFDATA%" merge -o "%PROF%\merged.profdata" "%PROF%" || exit /b 1

echo [4/4] optimized build
set "RUSTFLAGS=%BASEFLAGS% -C profile-use=%PROF%\merged.profdata"
cargo build --release %FEAT% --target-dir target\%NAME% || exit /b 1
for %%f in (target\%NAME%\x86_64-pc-windows-msvc\release\mixlauncher.exe) do echo %%f  %%~zf bytes
endlocal
