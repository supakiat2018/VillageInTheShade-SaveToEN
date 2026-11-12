@echo off
setlocal
cd /d "%~dp0"

if not exist bin mkdir bin
if not exist output mkdir output

echo [1/3] Compiling save_jp_to_en.dll (x64)...
zig cc -shared -O2 -s -m64 src\save_jp_to_en.c -o bin\save_jp_to_en.dll -lkernel32
if errorlevel 1 (
    echo [ERROR] Failed to compile save_jp_to_en.dll
    pause
    exit /b 1
)

echo [2/3] Compiling winmm.dll proxy (x64)...
zig cc -shared -O2 -s -m64 src\winmm_proxy.c src\winmm.def -o bin\winmm.dll -lkernel32
if errorlevel 1 (
    echo [ERROR] Failed to compile winmm.dll
    pause
    exit /b 1
)

echo [3/3] Packaging ZIP...
python scripts\make_zip.py
if errorlevel 1 (
    echo [ERROR] Failed to create ZIP package
    pause
    exit /b 1
)

echo.
echo [SUCCESS] Build and ZIP complete!
pause
