@echo off
setlocal

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe

if not exist build mkdir build
cd build

"%CMAKE%" .. -G "Visual Studio 17 2022" -A x64 ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_BUILD_TYPE=Debug

"%CMAKE%" --build . --config Debug --parallel

echo.
echo === Build complete ===
echo Binary: build\Debug\LlamaCode.exe
