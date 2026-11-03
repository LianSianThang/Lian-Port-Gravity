$ErrorActionPreference = 'Stop'
& (Join-Path $PSScriptRoot 'app\scripts\freeze_launcher.ps1') -Install
& python.exe (Join-Path $PSScriptRoot 'app\scripts\package_player.py')
if ($LASTEXITCODE -ne 0) { throw 'Player packaging failed' }
