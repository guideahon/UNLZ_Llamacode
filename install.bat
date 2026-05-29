@echo off
setlocal EnableExtensions EnableDelayedExpansion

echo === LlamaCode prerequisite installer ===

where winget >nul 2>&1
if errorlevel 1 (
    echo [ERROR] winget is required but not found.
    exit /b 1
)

where python >nul 2>&1
if errorlevel 1 (
    echo [ERROR] Python is required but not found.
    exit /b 1
)

set "CMAKE_EXE=C:\Program Files\CMake\bin\cmake.exe"
if not exist "%CMAKE_EXE%" (
    echo [STEP] Installing CMake...
    winget install --id Kitware.CMake --exact --source winget --accept-source-agreements --accept-package-agreements
    if errorlevel 1 (
        echo [ERROR] CMake installation failed.
        exit /b 1
    )
)

set "HAS_VS2022=0"
if exist "C:\BuildTools2022\MSBuild\Current\Bin\MSBuild.exe" set "HAS_VS2022=1"
if exist "C:\Program Files\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe" set "HAS_VS2022=1"
if "%HAS_VS2022%"=="0" (
    echo [STEP] Installing Visual Studio Build Tools 2022 (v143)...
    winget install --id Microsoft.VisualStudio.2022.BuildTools --exact --override "--quiet --wait --norestart --nocache --installPath C:\BuildTools2022 --add Microsoft.VisualStudio.Workload.VCTools --add Microsoft.VisualStudio.Component.VC.Tools.x86.x64 --add Microsoft.VisualStudio.Component.VC.v143.x86.x64 --add Microsoft.VisualStudio.Component.Windows11SDK.22621" --accept-source-agreements --accept-package-agreements
    if errorlevel 1 (
        echo [ERROR] Visual Studio Build Tools 2022 installation failed.
        exit /b 1
    )
)

if not exist "C:\Qt\6.8.3\msvc2022_64\lib\cmake\Qt6\Qt6Config.cmake" (
    echo [STEP] Installing Qt 6.8.3 (msvc2022_64)...
    python -m pip install --user aqtinstall
    if errorlevel 1 (
        echo [ERROR] Failed installing aqtinstall.
        exit /b 1
    )
    python -m aqt install-qt windows desktop 6.8.3 win64_msvc2022_64 -O C:\Qt
    if errorlevel 1 (
        echo [ERROR] Qt installation failed.
        exit /b 1
    )
)

echo.
echo [OK] Prerequisites installed.
echo Next step: run build.bat
exit /b 0
