param(
    [switch]$CheckOnly,
    [switch]$Sync
)

# Optional diagnostics only. Opens the launcher; never starts or controls a game.
$ErrorActionPreference = 'Stop'
$layerDir = Join-Path $PSScriptRoot 'tools\vulkan-validation\clang64\bin'
$runtimeDir = Join-Path $PSScriptRoot 'bin'
foreach ($name in @('VkLayer_khronos_validation.json', 'VkLayer_khronos_validation.dll',
                     'libSPIRV-Tools.dll', 'libSPIRV-Tools-opt.dll')) {
    if (-not (Test-Path -LiteralPath (Join-Path $layerDir $name))) {
        throw "Missing diagnostic dependency: $name"
    }
}
$savedEnv = @{}
foreach ($name in @('PATH', 'VK_LAYER_PATH', 'VK_INSTANCE_LAYERS',
                     'SPARTAN_VK_VALIDATION', 'SPARTAN_VK_VALIDATION_SYNC', 'SPARTAN_GPU_LOG')) {
    $savedEnv[$name] = [Environment]::GetEnvironmentVariable($name, 'Process')
}
try {
    $env:PATH = "$layerDir;$runtimeDir;" + $env:PATH
    $env:VK_LAYER_PATH = $layerDir
    $env:SPARTAN_VK_VALIDATION = '1'
    # Older staged runtimes classify validation warnings as info.
    $env:SPARTAN_GPU_LOG = 'info'
    $env:SPARTAN_VK_VALIDATION_SYNC = if ($Sync) { '1' } else { '0' }
    # The runtime enables the layer itself; this also enables it in vulkaninfo.
    $env:VK_INSTANCE_LAYERS = 'VK_LAYER_KHRONOS_validation'
    if ($CheckOnly) {
        $vulkanInfo = Join-Path $env:SystemRoot 'System32\vulkaninfo.exe'
        if (-not (Test-Path -LiteralPath $vulkanInfo)) { throw 'vulkaninfo.exe is unavailable.' }
        $logDir = Join-Path $PSScriptRoot 'logs'
        New-Item -ItemType Directory -Force -Path $logDir | Out-Null
        $log = Join-Path $logDir 'vulkan_validation_local_check.log'
        & $vulkanInfo --summary *> $log
        if ($LASTEXITCODE -ne 0) { throw "Validation check failed; see $log" }
        $output = Get-Content -LiteralPath $log -Raw
        if ($output -notmatch 'VK_LAYER_KHRONOS_validation' -or
            $output -match '(?im)\bERROR\b|Failed to load|Failed loading') {
            throw "Validation layer did not load cleanly; see $log"
        }
        Write-Output "Local validation layer check passed. Log: $log"
    } else {
        $pythonw = (Get-Command pythonw.exe -ErrorAction Stop).Source
        $launcher = Join-Path $PSScriptRoot 'GravityRushLauncher.py'
        Start-Process -FilePath $pythonw -ArgumentList ('"' + $launcher + '"') `
            -WorkingDirectory $PSScriptRoot -WindowStyle Hidden | Out-Null
        Write-Output 'Diagnostic launcher opened. Use Launch when ready; validation may reduce performance.'
    }
} finally {
    foreach ($name in $savedEnv.Keys) {
        [Environment]::SetEnvironmentVariable($name, $savedEnv[$name], 'Process')
    }
}
