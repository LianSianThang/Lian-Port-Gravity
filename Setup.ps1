param(
    [string]$GamePath,
    [string]$RuntimeDirectory,
    [switch]$Build,
    [switch]$CheckOnly
)
$ErrorActionPreference = 'Stop'
$app = Join-Path $PSScriptRoot 'app'
$python = (Get-Command python.exe -ErrorAction Stop).Source
if (-not $GamePath) { $GamePath = Join-Path $PSScriptRoot 'game\CUSA01130' }
if ($CheckOnly -and ($Build -or $RuntimeDirectory)) { throw 'CheckOnly cannot install or build a runtime.' }
if ($Build) {
    & (Join-Path $app 'Build_Gravity_Runtime.ps1')
    & (Join-Path $app 'scripts\build_launcher_start.ps1')
}
if ($RuntimeDirectory) {
    & $python (Join-Path $app 'scripts\runtime_files.py') --install $RuntimeDirectory --destination (Join-Path $app 'bin')
    if ($LASTEXITCODE -ne 0) { throw 'Runtime bundle installation failed' }
}
$arguments = @((Join-Path $app 'player_setup.py'), '--game', $GamePath)
if ($CheckOnly) { $arguments += '--check' }
& $python @arguments
if ($LASTEXITCODE -ne 0) { throw 'Game validation/preparation failed' }
& $python (Join-Path $app 'scripts\check_player_runtime.py')
if ($LASTEXITCODE -ne 0) { throw 'Runtime files are incomplete' }
Write-Output 'Setup checks passed. Open Launch_Lian_Port.exe in this repository folder when ready.'
