@echo off
setlocal EnableExtensions EnableDelayedExpansion

echo === LlamaCode install ^& build ===
echo.

REM ============================================================
REM  1) PREREQUISITES: check, install the missing ones
REM ============================================================

REM --- winget (required, cannot auto-install) ---
where winget >nul 2>&1
if errorlevel 1 (
    echo [ERROR] winget is required but not found. Install "App Installer" from the Microsoft Store.
    pause & exit /b 1
)

REM --- Python (required for aqtinstall, cannot auto-install) ---
where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python is required but not found. Install it from https://python.org and re-run.
    pause & exit /b 1
)

REM --- CMake ---
set "CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe"
if not exist "%CMAKE%" set "CMAKE=C:\Program Files (x86)\Microsoft Visual Studio\2019\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist "%CMAKE%" (
    echo [STEP] Installing CMake...
    winget install --id Kitware.CMake --exact --source winget --accept-source-agreements --accept-package-agreements
    if errorlevel 1 ( echo [ERROR] CMake installation failed. & pause & exit /b 1 )
    set "CMAKE=%PROGRAMFILES%\CMake\bin\cmake.exe"
)
if not exist "%CMAKE%" (
    echo [ERROR] CMake not found after install attempt.
    pause & exit /b 1
)
echo [OK] CMake: "%CMAKE%"

REM --- Visual Studio Build Tools 2022 (v143) ---
set "HAS_VS2022=0"
if exist "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe" set "HAS_VS2022=1"
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set "HAS_VS2022=1"
if "!HAS_VS2022!"=="0" (
    echo [STEP] Installing Visual Studio Build Tools 2022 (v143)...
    winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override "--quiet --wait --norestart --nocache --installPath C:\BuildTools2022 --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.v143.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621" --accept-source-agreements --accept-package-agreements
    if errorlevel 1 ( echo [ERROR] Visual Studio Build Tools 2022 installation failed. & pause & exit /b 1 )
)
echo [OK] Visual Studio Build Tools 2022

REM --- Qt 6.8.3 (msvc2022_64) ---
set "QT_DIR=C:\Qt\6.8.3\msvc2022_64"
if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [STEP] Installing Qt 6.8.3 (msvc2022_64)...
    python -m pip install --user aqtinstall
    if errorlevel 1 ( echo [ERROR] Failed installing aqtinstall. & pause & exit /b 1 )
    python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
    if errorlevel 1 ( echo [ERROR] Qt installation failed. & pause & exit /b 1 )
)
if not exist "%QT_DIR%\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [ERROR] Qt6 not found at %QT_DIR% after install attempt.
    pause & exit /b 1
)
echo [OK] Qt6: %QT_DIR%

echo.
echo [OK] All prerequisites ready.
echo.

REM ============================================================
REM  2) BUILD
REM ============================================================

set "GENERATOR=Visual Studio 16 2019"
if exist "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe" set "GENERATOR=Visual Studio 17 2022"
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set "GENERATOR=Visual Studio 17 2022"
echo [INFO] Generator: %GENERATOR%

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
timeout /t 2 /nobreak >nul

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

"%CMAKE%" .. -G "%GENERATOR%" -A x64 ^
    -DCMAKE_PREFIX_PATH="%QT_DIR%" ^
    -DCMAKE_BUILD_TYPE=Debug
if errorlevel 1 (
    echo.
    echo === CMake configure FAILED ===
    cd ..
    pause
    exit /b 1
)

"%CMAKE%" --build . --config Debug -- /maxcpucount:4
if errorlevel 1 (
    echo.
    echo === Build FAILED ===
    cd ..
    pause
    exit /b 1
)

cd ..

set "WINDEPLOYQT=%QT_DIR%\bin\windeployqt.exe"
set "EXE_DIR=%~dp0build\Debug"
set "EXE_PATH=%EXE_DIR%\LlamaCode.exe"
if not exist "%WINDEPLOYQT%" (
    echo [ERROR] windeployqt.exe not found at %WINDEPLOYQT%
    pause & exit /b 1
)
if not exist "%EXE_PATH%" (
    echo [ERROR] Built executable not found at %EXE_PATH%
    pause & exit /b 1
)

echo [INFO] Deploying Qt runtime next to LlamaCode.exe...
"%WINDEPLOYQT%" --debug --qmldir "%~dp0qml" --no-translations --compiler-runtime "%EXE_PATH%"
if errorlevel 1 (
    echo.
    echo === windeployqt FAILED ===
    pause
    exit /b 1
)

echo [INFO] Copying Qt.labs.settings manually...
xcopy /E /I /Y "%QT_DIR%\qml\Qt\labs\settings" "%EXE_DIR%\qml\Qt\labs\settings" >nul

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0update-shortcut.ps1"

echo.
echo === Install ^& build complete ===
echo Binary: build\Debug\LlamaCode.exe
pause
