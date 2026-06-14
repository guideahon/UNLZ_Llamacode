@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM Configura, compila y corre TODA la suite de tests (unit + integration).
REM Uso: tests.bat [Debug|Release]   (default: Debug)
REM Sin 'pause' al final → seguro para correr en CI / desde scripts.

set CFG=%1
if "%CFG%"=="" set CFG=Debug

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe
if not exist "%CMAKE%" set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set CTEST=%PROGRAMFILES%\CMake\bin\ctest.exe
if not exist "%CTEST%" set CTEST=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe

if not exist "%CMAKE%" ( echo [ERROR] CMake not found. & exit /b 1 )
if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" ( echo [ERROR] Qt6 not found at %QT_DIR% & exit /b 1 )

REM Build dir separado para no pisar la build del app. En el primer configure
REM dejamos que CMake elija su generador default (la última VS instalada); en
REM reconfiguraciones reusa el generador cacheado.
if not exist build_tests mkdir build_tests

if exist build_tests\CMakeCache.txt (
    "%CMAKE%" -S . -B build_tests -DCMAKE_PREFIX_PATH="%QT_DIR%" -DBUILD_TESTS=ON
) else (
    "%CMAKE%" -S . -B build_tests -A x64 -DCMAKE_PREFIX_PATH="%QT_DIR%" -DBUILD_TESTS=ON
)
if errorlevel 1 ( echo. & echo === Configure FAILED === & exit /b 1 )

"%CMAKE%" --build build_tests --config %CFG% -- /maxcpucount:4
if errorlevel 1 ( echo. & echo === Build FAILED === & exit /b 1 )

REM Los test exes necesitan las DLLs de Qt en PATH (no se hace windeployqt).
set PATH=%QT_DIR%\bin;%PATH%

echo.
echo [INFO] ===== Running ctest (%CFG%) =====
"%CTEST%" --test-dir build_tests -C %CFG% --output-on-failure
if errorlevel 1 ( echo. & echo === TESTS FAILED === & exit /b 1 )

echo.
echo === All tests passed ===
exit /b 0
