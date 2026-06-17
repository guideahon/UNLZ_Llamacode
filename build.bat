@echo off
setlocal EnableDelayedExpansion
cd /d "%~dp0"

REM Usage: build.bat [Debug|Release|Both] [NOPAUSE]   (default: Both)
set CONFIGS=%1
if "%CONFIGS%"=="" set CONFIGS=Both
set NO_PAUSE=0
if /I "%2"=="NOPAUSE" set NO_PAUSE=1

set QT_DIR=C:\Qt\6.8.3\msvc2022_64
set CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe
if not exist "%CMAKE%" set CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe
set GENERATOR=Visual Studio 16 2019
if exist "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set GENERATOR=Visual Studio 17 2022

if not exist "%CMAKE%" (
    echo [ERROR] CMake not found.
    goto :failed
)

if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [ERROR] Qt6 not found at %QT_DIR%
    goto :failed
)

echo [INFO] Killing LlamaCode and managed children...
taskkill /F /IM LlamaCode.exe      >nul 2>&1
taskkill /F /IM llama-server.exe   >nul 2>&1
taskkill /F /IM opencode.exe       >nul 2>&1
taskkill /F /IM aider.exe          >nul 2>&1

echo [INFO] Killing stale build tools...
taskkill /F /IM MSBuild.exe        >nul 2>&1
taskkill /F /IM CL.exe             >nul 2>&1
taskkill /F /IM link.exe           >nul 2>&1
taskkill /F /IM rc.exe             >nul 2>&1
taskkill /F /IM qmlcachegen.exe    >nul 2>&1
taskkill /F /IM rcc.exe            >nul 2>&1
taskkill /F /IM moc.exe            >nul 2>&1
ping -n 3 127.0.0.1 >nul 2>&1

echo [INFO] Clearing stale tlogs...
if exist build (
    for /r "build" %%f in (*.tlog) do del /f /q "%%f" >nul 2>&1
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

REM VS is a multi-config generator: configure once, build per --config below.
"%CMAKE%" .. -G "%GENERATOR%" -A x64 ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%"
if errorlevel 1 (
    echo.
    echo === Configure FAILED ===
    goto :failed
)

cd ..

set DID_DEBUG=0
set DID_RELEASE=0

if /I "%CONFIGS%"=="Both"    ( call :build_one Debug && call :build_one Release ) else ( call :build_one %CONFIGS% )
if errorlevel 1 (
    goto :failed
)

REM Shortcuts
if "%DID_RELEASE%"=="1" (
    echo [INFO] Updating Release shortcut...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update-shortcut.ps1" -Config Release -ShortcutName "LlamaCode" -Icon "assets\app_icon.ico"
)
if "%DID_DEBUG%"=="1" (
    echo [INFO] Updating Debug shortcut...
    powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update-shortcut.ps1" -Config Debug -ShortcutName "LlamaCode-Debug" -Icon "assets\debug_icon.ico"
)

echo.
echo === Build complete ===
if "%DID_RELEASE%"=="1" echo Release: build\Release\LlamaCode.exe - shortcut: LlamaCode.lnk
if "%DID_DEBUG%"=="1" echo Debug: build\Debug\LlamaCode.exe - shortcut: LlamaCode-Debug.lnk
goto :success

REM Build + deploy one config
:build_one
set CFG=%~1
echo.
echo [INFO] ===== Building %CFG% =====
"%CMAKE%" --build build --config %CFG% -- /maxcpucount:4
if errorlevel 1 ( echo. & echo === %CFG% Build FAILED === & exit /b 1 )

set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
set "EXE_DIR=%~dp0build\%CFG%"
set "EXE_PATH=%EXE_DIR%\LlamaCode.exe"
if not exist "%WINDEPLOYQT%" ( echo [ERROR] windeployqt not found & exit /b 1 )
if not exist "%EXE_PATH%"    ( echo [ERROR] %EXE_PATH% missing & exit /b 1 )

set DEPLOY_FLAG=--release
if /I "%CFG%"=="Debug" set DEPLOY_FLAG=--debug

echo [INFO] Deploying Qt runtime (%CFG%)...
"%WINDEPLOYQT%" %DEPLOY_FLAG% --qmldir "%~dp0qml" --no-translations --compiler-runtime "%EXE_PATH%" >nul
if errorlevel 1 ( echo. & echo === windeployqt %CFG% FAILED === & exit /b 1 )

echo [INFO] Copying Qt.labs.settings (%CFG%)...
xcopy /E /I /Y "%QT_DIR%\qml\Qt\labs\settings" "%EXE_DIR%\qml\Qt\labs\settings" >nul

if /I "%CFG%"=="Debug"   set DID_DEBUG=1
if /I "%CFG%"=="Release" set DID_RELEASE=1
exit /b 0

:failed
if "%NO_PAUSE%"=="0" pause
exit /b 1

:success
if "%NO_PAUSE%"=="0" pause
exit /b 0
