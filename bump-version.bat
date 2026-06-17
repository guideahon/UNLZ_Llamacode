@echo off
setlocal
cd /d "%~dp0"

REM Usage:
REM   bump-version.bat 0.1.2
REM   bump-version.bat 0.1.2 --summary "Short release summary"
REM   bump-version.bat 0.1.2 --changelog "Item one|Item two|Item three"
REM   bump-version.bat 0.1.2 --no-update-flag

if "%~1"=="" goto :usage
set "NEW_VERSION=%~1"
shift

set "SUMMARY="
set "CHANGELOG="
set "NO_UPDATE_FLAG=0"

:parse
if "%~1"=="" goto :run
if /I "%~1"=="--summary" goto :set_summary
if /I "%~1"=="--changelog" goto :set_changelog
if /I "%~1"=="--no-update-flag" goto :set_no_update_flag
echo [ERROR] Unknown option: %~1
goto :usage

:set_summary
shift
if "%~1"=="" goto :usage
set "SUMMARY=%~1"
shift
goto :parse

:set_changelog
shift
if "%~1"=="" goto :usage
set "CHANGELOG=%~1"
shift
goto :parse

:set_no_update_flag
set "NO_UPDATE_FLAG=1"
shift
goto :parse

:run
powershell -NoProfile -ExecutionPolicy Bypass -Command ^
  "$ErrorActionPreference='Stop';" ^
  "$version=$env:NEW_VERSION;" ^
  "$q=[char]34;" ^
  "if ($version -notmatch '^\d+\.\d+\.\d+([-.][0-9A-Za-z.-]+)?$') { throw 'Version must look like 1.2.3 or 1.2.3-beta.1' }" ^
  "$root=(Get-Location).Path;" ^
  "function Set-Text($path,[scriptblock]$edit) { $full=Join-Path $root $path; $text=[IO.File]::ReadAllText($full); $new=& $edit $text; if ($new -eq $text) { Write-Host ('[SKIP] '+$path) } else { [IO.File]::WriteAllText($full,$new,[Text.UTF8Encoding]::new($false)); Write-Host ('[OK] '+$path) } }" ^
  "Set-Text 'CMakeLists.txt' { param($t) $t -replace 'project\(LlamaCode VERSION [^) ]+ LANGUAGES CXX\)', ('project(LlamaCode VERSION '+$version+' LANGUAGES CXX)') };" ^
  "Set-Text 'src/main.cpp' { param($t) $t -replace 'app\.setApplicationVersion\(\x22[^\x22]+\x22\);', ('app.setApplicationVersion('+$q+$version+$q+');') };" ^
  "Set-Text 'src/AppController.h' { param($t) $t -replace 'Q_INVOKABLE QString version\(\) const \{ return QStringLiteral\(\x22[^\x22]+\x22\); \}', ('Q_INVOKABLE QString version() const { return QStringLiteral('+$q+$version+$q+'); }') };" ^
  "$jsonPath=Join-Path $root 'assets/update/latest.json';" ^
  "$json=Get-Content $jsonPath -Raw | ConvertFrom-Json;" ^
  "$json.version=$version;" ^
  "$json.newVersion=($env:NO_UPDATE_FLAG -ne '1');" ^
  "if ($env:SUMMARY) { $json.summary=$env:SUMMARY }" ^
  "if ($env:CHANGELOG) { $json.changelog=@($env:CHANGELOG -split '\|' | ForEach-Object { $_.Trim() } | Where-Object { $_ }) }" ^
  "$out=$json | ConvertTo-Json -Depth 8;" ^
  "$out=$out -replace '(?m)^                      (?=\S)', '    ' -replace '(?m)^                  (?=\])', '  ' -replace '(?m)^    (?=\S.+:)', '  ' -replace ':  ', ': ';" ^
  "[IO.File]::WriteAllText($jsonPath,$out+[Environment]::NewLine,[Text.UTF8Encoding]::new($false));" ^
  "Write-Host '[OK] assets/update/latest.json';" ^
  "Write-Host ('Bumped LlamaCode to '+$version)"
if errorlevel 1 exit /b 1

echo.
echo [INFO] Review changes with: git diff
exit /b 0

:usage
echo Usage:
echo   bump-version.bat VERSION [--summary "text"] [--changelog "a|b|c"] [--no-update-flag]
echo.
echo Examples:
echo   bump-version.bat 0.1.2
echo   bump-version.bat 0.1.2 --summary "Release notes" --changelog "Fix A|Add B"
exit /b 1
