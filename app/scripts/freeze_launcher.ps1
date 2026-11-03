param([string]$Python = 'python.exe', [switch]$Install)
$ErrorActionPreference = 'Stop'
$app = Split-Path -Parent $PSScriptRoot
$out = Join-Path $app 'out'
$temporaryBuildPath = Join-Path $out 'launcher-build-temp'
New-Item -ItemType Directory -Path $temporaryBuildPath -Force | Out-Null
$oldTemp = $env:TEMP
$oldTmp = $env:TMP
$oldFreezerCache = $env:PYINSTALLER_CONFIG_DIR
try {
    $env:TEMP = $temporaryBuildPath
    $env:TMP = $temporaryBuildPath
    $env:PYINSTALLER_CONFIG_DIR = Join-Path $out 'pyinstaller-cache'
    $venv = Join-Path $out 'launcher-build-venv'
    $builder = Join-Path $venv 'Scripts\python.exe'
    if (-not (Test-Path -LiteralPath $builder)) {
        & $Python -m venv $venv
        if ($LASTEXITCODE -ne 0) { throw 'Launcher build environment creation failed' }
    }
    & $builder -c 'import PyInstaller; assert PyInstaller.__version__ == "6.22.3"' 2>$null
    if ($LASTEXITCODE -ne 0) {
        & $builder -m ensurepip --upgrade --default-pip
        if ($LASTEXITCODE -ne 0) { throw 'Private pip initialization failed' }
        & $builder -m pip install 'PyInstaller==6.22.3'
        if ($LASTEXITCODE -ne 0) { throw 'PyInstaller installation failed' }
    }
    & $builder -m PyInstaller --noconfirm --windowed --onedir --noupx `
        --name Launch_Lian_Port --icon (Join-Path $app 'branding\lianport.ico') `
        --distpath (Join-Path $out 'frozen') --workpath (Join-Path $out 'freeze-work') `
        --specpath (Join-Path $out 'freeze-spec') `
        --add-data ((Join-Path $app 'data') + ';data') `
        (Join-Path $app 'GravityRushLauncher.py')
    if ($LASTEXITCODE -ne 0) { throw 'Frozen launcher build failed' }
    if ($Install) {
        $repository = Split-Path -Parent $app
        $bundle = Join-Path $out 'frozen\Launch_Lian_Port'
        $target = Join-Path $repository 'Launch_Lian_Port.exe'
        if (Test-Path -LiteralPath $target) {
            $backup = Join-Path $repository ('local-data\launcher-backup-' + (Get-Date -Format yyyyMMdd-HHmmss) + '.exe')
            New-Item -ItemType Directory -Path (Split-Path -Parent $backup) -Force | Out-Null
            Copy-Item -LiteralPath $target -Destination $backup
        }
        Copy-Item -LiteralPath (Join-Path $bundle '_internal') -Destination $repository -Recurse -Force
        Copy-Item -LiteralPath (Join-Path $bundle 'Launch_Lian_Port.exe') -Destination $target -Force
    }
    Write-Output ('Frozen launcher ready: ' + (Join-Path $out 'frozen\Launch_Lian_Port'))
} finally {
    $env:TEMP = $oldTemp
    $env:TMP = $oldTmp
    $env:PYINSTALLER_CONFIG_DIR = $oldFreezerCache
}
