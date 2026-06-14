# Compila y corre los selftests del tuner (core + engine Qt) con MSVC 2022 + Qt 6.8.3.
$ErrorActionPreference = "Stop"
$root = Split-Path $PSScriptRoot -Parent
$qt   = "C:\Qt\6.8.3\msvc2022_64"
$vcvars = "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
$out  = Join-Path $root "build_tuner"
New-Item -ItemType Directory -Force $out | Out-Null

# moc para TunerEngine (Q_OBJECT)
& "$qt\bin\moc.exe" "$root\src\core\tuner\TunerEngine.h" -o "$out\moc_TunerEngine.cpp"

$inc = "/I `"$root\src`" /I `"$qt\include`" /I `"$qt\include\QtCore`" /I `"$qt\include\QtNetwork`""
$libs = "`"$qt\lib\Qt6Core.lib`" `"$qt\lib\Qt6Network.lib`""

# Selftest 1: core puro (sin Qt)
$c1 = "cl /nologo /std:c++17 /EHsc /I `"$root\src`" `"$root\tools\tuner_selftest.cpp`" `"$root\src\core\tuner\AutoTuner.cpp`" /Fe:`"$out\tuner_selftest.exe`" /Fo:`"$out\\`""
# Selftest 2: engine Qt (QtCore + QtNetwork)
$c2 = "cl /nologo /std:c++17 /Zc:__cplusplus /permissive- /EHsc /MD $inc `"$root\tools\tuner_engine_selftest.cpp`" `"$root\src\core\tuner\TunerEngine.cpp`" `"$root\src\core\tuner\AutoTuner.cpp`" `"$out\moc_TunerEngine.cpp`" /Fe:`"$out\tuner_engine_selftest.exe`" /Fo:`"$out\\`" /link $libs"

cmd /c "`"$vcvars`" >nul 2>&1 && $c1 && $c2"
if ($LASTEXITCODE -ne 0) { Write-Error "build failed"; exit 1 }

$env:PATH = "$qt\bin;$env:PATH"
& "$out\tuner_selftest.exe";        $e1 = $LASTEXITCODE
& "$out\tuner_engine_selftest.exe"; $e2 = $LASTEXITCODE
if ($e1 -ne 0 -or $e2 -ne 0) { Write-Error "selftest failed (core=$e1 engine=$e2)"; exit 1 }
Write-Host "ALL TUNER TESTS PASSED"
