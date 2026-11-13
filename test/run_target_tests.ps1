param([string]$PlatformioPython, [string]$Port = $env:RACOOL_SERIAL_PORT)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (!$PlatformioPython) {
    $platformCore = $env:PLATFORMIO_CORE_DIR
    if (!$platformCore) {
        $platformCore = Join-Path ([Environment]::GetFolderPath('UserProfile')) '.platformio'
    }
    $pythonRelative = if ([Environment]::OSVersion.Platform -eq 'Win32NT') { 'penv/Scripts/python.exe' } else { 'penv/bin/python' }
    $PlatformioPython = Join-Path $platformCore $pythonRelative
}
if (!(Test-Path -LiteralPath $PlatformioPython)) {
    throw 'PlatformIO Python was not found. Pass -PlatformioPython or configure PLATFORMIO_CORE_DIR.'
}
$PlatformioPython = (Resolve-Path -LiteralPath $PlatformioPython).ProviderPath
function Test-BuildDiagnostics([string]$LogPath) {
    $logText = Get-Content -LiteralPath $LogPath -Raw
    # Turkish casing must not make uppercase WARNING invisible to validation.
    $options = [System.Text.RegularExpressions.RegexOptions]::IgnoreCase -bor [System.Text.RegularExpressions.RegexOptions]::CultureInvariant
    foreach ($line in ($logText -split '\r?\n')) {
        # Runtime fault-injection/configuration logs are not compiler diagnostics.
        if ([regex]::IsMatch($line, '^\s*[IDEWV]\s*\([^)]*\)', $options)) { continue }
        if ([regex]::IsMatch($line, '\b(?:warning|error)\s*:|CMake Warning|\bWarning[!:]|\b[A-Za-z]+Warning:|^\s*\[(?:Warn|Error)\]', $options)) { return $true }
    }
    return $false
}
$testExit = 1
$restoreExit = 1
$originalBuildDirectory = $env:PLATFORMIO_BUILD_DIR
$testPortArguments = @()
$uploadPortArguments = @()
if ($Port) {
    $testPortArguments = @('--upload-port', $Port, '--test-port', $Port)
    $uploadPortArguments = @('--upload-port', $Port)
}
if (!$originalBuildDirectory) { $env:PLATFORMIO_BUILD_DIR = Join-Path $projectRoot 'test/build/firmware_verification' }
Push-Location $projectRoot
try {
    node test/check_test_inventory.mjs
    if ($LASTEXITCODE -ne 0) { throw 'Test inventory validation failed.' }
    try {
        $targetLogPath = Join-Path $projectRoot 'test/build/target-last-run.log'
        # Windows PowerShell treats native stderr as an ErrorRecord. Capture the
        # complete compiler diagnostic, then use the native exit code for acceptance.
        try {
            $ErrorActionPreference = 'Continue'
            & $PlatformioPython -m platformio test -vv -e esp32-c6-devkitc-1_test @testPortArguments 2>&1 | Tee-Object -FilePath $targetLogPath
            $testExit = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = 'Stop'
        }
        if (Test-BuildDiagnostics $targetLogPath) {
            Write-Output 'Target build/upload diagnostics failed zero-warning/error acceptance.'
            $testExit = 1
        }
        if ($testExit -eq 0) {
            node test/check_target_results.mjs $targetLogPath
            $testExit = $LASTEXITCODE
        }
    } finally {
        Write-Output 'Restoring production firmware after target tests.'
        $restoreLogPath = Join-Path $projectRoot 'test/build/target-restore-last-run.log'
        try {
            $ErrorActionPreference = 'Continue'
            & $PlatformioPython -m platformio run -e esp32-c6-devkitc-1 -t upload @uploadPortArguments 2>&1 | Tee-Object -FilePath $restoreLogPath
            $restoreExit = $LASTEXITCODE
        } finally {
            $ErrorActionPreference = 'Stop'
        }
        if (Test-BuildDiagnostics $restoreLogPath) {
            Write-Output 'Production restoration diagnostics failed zero-warning/error acceptance.'
            $restoreExit = 1
        }
    }
} finally {
    Pop-Location
    $env:PLATFORMIO_BUILD_DIR = $originalBuildDirectory
}
if ($testExit -ne 0 -or $restoreExit -ne 0) { exit 1 }
exit 0
