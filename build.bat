@echo off
setlocal EnableDelayedExpansion

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe
if not exist "%CMAKE%" set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set GENERATOR=Visual Studio 16 2019
if exist "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022

if not exist "%CMAKE%" (
    echo [ERROR] CMake not found.
    exit /b 1
)

if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [ERROR] Qt6 not found at %QT_DIR%
    exit /b 1
)

if not exist build mkdir build
cd build

if exist CMakeCache.txt (
    for /f "tokens=2 delims==" %%G in ('findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" CMakeCache.txt') do set "CACHE_GENERATOR=%%G"
    if defined CACHE_GENERATOR (
        if /I not "!CACHE_GENERATOR!"=="%GENERATOR%" (
            echo [INFO] Generator changed from "!CACHE_GENERATOR!" to "%GENERATOR%". Resetting CMake cache...
            del /f /q CMakeCache.txt >nul 2>&1
            rmdir /s /q CMakeFiles >nul 2>&1
        )
    )
)

"%CMAKE%" .. -G "%GENERATOR%" -A x64 ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_BUILD_TYPE=Debug

"%CMAKE%" --build . --config Debug --parallel
if errorlevel 1 exit /b 1

cd ..
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update-shortcut.ps1"

echo.
echo === Build complete ===
echo Binary: build\Debug\LlamaCode.exe
