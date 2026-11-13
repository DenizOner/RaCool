param(
    [Parameter(Mandatory=$true)][string]$Port,
    [string]$OutputPrefix = 'test/build/i2c-wait-live'
)
function Invoke-CheckedNative([string]$Executable, [string[]]$Arguments) {
    $savedPreference = $ErrorActionPreference
    try {
        $ErrorActionPreference = 'Continue'
        & $Executable @Arguments 2>&1
        $nativeExit = $LASTEXITCODE
    } finally { $ErrorActionPreference = $savedPreference }
    if ($nativeExit -ne 0) { throw "Native command failed with exit code $nativeExit." }
}
$ErrorActionPreference = 'Stop'
$root = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$core = if ($env:PLATFORMIO_CORE_DIR) { $env:PLATFORMIO_CORE_DIR } else { Join-Path $env:USERPROFILE '.platformio' }
$pioPython = Join-Path $core 'penv/Scripts/python.exe'
$serialPython = Join-Path $root '.venv/Scripts/python.exe'
$previousBuild = $env:PLATFORMIO_BUILD_DIR
$env:PLATFORMIO_BUILD_DIR = Join-Path $root 'test/build/firmware_verification'
Push-Location $root
try {
    try {
        Invoke-CheckedNative $pioPython @('-m', 'platformio', 'run', '-e', 'esp32-c6-i2c-trace', '-t', 'upload', '--upload-port', $Port)
        Invoke-CheckedNative $serialPython @('test/Communication/DebugConsole/diagnostic/serial_capture.py', '--port', $Port, '--duration', '70', '--output', "$OutputPrefix-boot.log")
        Invoke-CheckedNative $serialPython @('test/Communication/DebugConsole/diagnostic/serial_capture.py', '--port', $Port, '--duration', '70', '--output', "$OutputPrefix-restart.log", '--command', 'power restart 500')
        Invoke-CheckedNative $serialPython @('test/Communication/DebugConsole/diagnostic/serial_capture.py', '--port', $Port, '--duration', '20', '--output', "$OutputPrefix-health.log", '--developer', '--command', 'UI TEST', '--command', 'ir_temp stats', '--command', 'watchdog status')
    } finally {
        Write-Output 'Restoring standard production firmware (no semaphore instrumentation).'
        Invoke-CheckedNative $pioPython @('-m', 'platformio', 'run', '-e', 'esp32-c6-devkitc-1', '-t', 'upload', '--upload-port', $Port)
    }
} finally {
    Pop-Location
    $env:PLATFORMIO_BUILD_DIR = $previousBuild
}
