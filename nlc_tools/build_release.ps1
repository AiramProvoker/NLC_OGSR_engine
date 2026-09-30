# NLC: build xrEngine.exe (Release x64) into bin_x64 of this repository.
#
# Requirements: VS 2022 Build Tools (MSVC v143) with the "C++ ATL for latest
# v143 build tools" component, a Windows 10/11 SDK, ~10 GB free disk space, and
# dependencies from nlc_tools\fetch_deps.ps1.
#
# Usage:
#   powershell -NoProfile -ExecutionPolicy Bypass -File nlc_tools\build_release.ps1 [-Rebuild]
#
# Build environment notes:
# - The projects compile with /source-charset:utf-8 and /we4566, so narrow string
#   literals must be representable in the execution charset. OGSR and NLC are
#   built on Russian Windows (ANSI code page 1251); CL=/execution-charset:windows-1251
#   reproduces that on any system locale (arc_sqfs.cpp has Cyrillic FATAL text).
# - NoDefaultCurrentDirectoryInExePath must be unset: DirectXTex's CompileShaders
#   and LuaJIT's msvcbuild.bat are started from their working directory.
# - XR_3DA's PostBuildStep1 copies dbghelp.dll from the Windows SDK Debuggers
#   folder. Without the SDK debugging tools it fails after the exe is linked;
#   that single error is tolerated here (the game ships its own dbghelp.dll).

param([switch]$Rebuild)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$vcvars = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path $vcvars)) { throw "vcvars64.bat not found: $vcvars" }

$logDir = Join-Path $root 'nlc_tools\logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$log = Join-Path $logDir ('build_' + (Get-Date -Format 'yyyyMMdd_HHmmss') + '.log')
$exe = Join-Path $root 'bin_x64\xrEngine.exe'
$start = Get-Date

$target = if ($Rebuild) { '/t:Rebuild' } else { '/t:Build' }
$cmd = 'set NoDefaultCurrentDirectoryInExePath=&& set CL=/execution-charset:windows-1251&& ' +
       'call "' + $vcvars + '" >nul && cd /d "' + $root + '" && ' +
       'msbuild Engine.sln ' + $target + ' /m /nologo /v:m /p:Configuration=Release /p:Platform=x64 > "' + $log + '" 2>&1'
cmd.exe /d /c $cmd
$code = $LASTEXITCODE

$allErrors = @(Select-String -Path $log -Pattern ': (fatal )?error ' | ForEach-Object { $_.Line } | Sort-Object -Unique)
$dbghelp = @($allErrors | Where-Object { $_ -match 'copy /Y .*Debuggers.*dbghelp\.dll' })
$errors = @($allErrors | Where-Object { $_ -notmatch 'copy /Y .*Debuggers.*dbghelp\.dll' })
$linked = (Test-Path $exe) -and ((Get-Item $exe).LastWriteTime -ge $start.AddSeconds(-5))

if ($errors.Count -gt 0) {
    $errors | Select-Object -First 30 | ForEach-Object { Write-Host $_ }
    throw "Build failed ($($errors.Count) error lines). Log: $log"
}
if ($code -ne 0 -and $dbghelp.Count -eq 0) { throw "MSBuild exit $code without a recognised error line. Log: $log" }
if (-not (Test-Path $exe)) { throw "xrEngine.exe was not produced. Log: $log" }

$item = Get-Item $exe
Write-Host ("Built {0}  {1:N0} bytes  {2}" -f $item.FullName, $item.Length, $item.LastWriteTime)
Write-Host ("SHA-256 {0}" -f (Get-FileHash $exe -Algorithm SHA256).Hash)
if (-not $linked) { Write-Host 'Note: xrEngine.exe was already up to date (not relinked).' }
Write-Host "Log: $log"
