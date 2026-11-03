param([switch]$ConfigureOnly)
$ErrorActionPreference = 'Stop'
$repository = Split-Path -Parent $PSScriptRoot
$runtime = Join-Path $repository 'runtime'
$msysRoot = if ($env:MSYS2_ROOT) { $env:MSYS2_ROOT } else { 'C:\msys64' }
$compilerBin = Join-Path $msysRoot 'clang64\bin'
$cmake = Join-Path $compilerBin 'cmake.exe'
$ninja = Join-Path $compilerBin 'ninja.exe'
$env:PATH = $compilerBin + ';' + $env:PATH
$build = Join-Path $runtime 'out\gpu'
& $cmake -S (Join-Path $runtime 'gpu') -B $build -G Ninja `
    -DCMAKE_BUILD_TYPE=RelWithDebInfo -DSPARTAN_PGO=off -DSPARTAN_LTO=OFF `
    -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++
if ($LASTEXITCODE -ne 0) { throw 'Runtime configuration failed' }
if ($ConfigureOnly) { return }
& $ninja -C $build spartanport-probe.exe
if ($LASTEXITCODE -ne 0) { throw 'Runtime build failed; installed runtime left untouched' }
$bin = Join-Path $PSScriptRoot 'bin'
New-Item -ItemType Directory -Path $bin -Force | Out-Null
$target = Join-Path $bin 'gravity-probe-local.exe'
if (Test-Path -LiteralPath $target) {
    Copy-Item -LiteralPath $target -Destination ($target + '.' + (Get-Date -Format yyyyMMdd-HHmmss) + '.bak')
}
$helper = Join-Path $build 'lianport-guest-start.exe'
& (Join-Path $compilerBin 'clang.exe') -std=c11 -O2 -Wall -Wextra -Werror -municode `
    (Join-Path $runtime 'src\guest_start_win.c') -o $helper
if ($LASTEXITCODE -ne 0) { throw 'Guest-start helper build failed' }
& python.exe (Join-Path $PSScriptRoot 'scripts\stage_runtime.py') `
    --probe (Join-Path $build 'spartanport-probe.exe') --helper $helper `
    --compiler-bin $compilerBin --destination $bin
if ($LASTEXITCODE -ne 0) { throw 'Runtime staging failed' }
Write-Output "Installed local runtime and dependencies: $target"
