$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
$repository = Split-Path -Parent $project
$launcherExe = Join-Path $repository 'Launch_Lian_Port.exe'
$msysRoot = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' }
$compilerBin = Join-Path $msysRoot 'clang64\bin'
$env:PATH = $compilerBin + ';' + $env:PATH
Push-Location $PSScriptRoot
try {
    New-Item -ItemType Directory -Path (Join-Path $project 'out') -Force | Out-Null
    & (Join-Path $compilerBin 'windres.exe') -i launcher_start.rc -o ../out/launcher_start.res.o
    if ($LASTEXITCODE -ne 0) { throw 'Launcher icon build failed' }
    & (Join-Path $compilerBin 'clang.exe') -std=c11 -Wall -Wextra -Werror -municode -mwindows `
        launcher_start.c ../out/launcher_start.res.o -o $launcherExe -luser32
    if ($LASTEXITCODE -ne 0) { throw 'Launcher executable build failed' }
    Write-Output $launcherExe
} finally {
    Pop-Location
}
