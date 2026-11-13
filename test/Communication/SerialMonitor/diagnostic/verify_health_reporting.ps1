param([Parameter(Mandatory=$true)][string]$Port)
$ErrorActionPreference = 'Stop'
$taskPython = if ($env:RACOOL_PYTHON) { $env:RACOOL_PYTHON } `
              elseif ($env:PLATFORMIO_CORE_DIR -and (Test-Path (Join-Path $env:PLATFORMIO_CORE_DIR 'penv/Scripts/python.exe'))) { Join-Path $env:PLATFORMIO_CORE_DIR 'penv/Scripts/python.exe' } `
              elseif (Test-Path (Join-Path $env:USERPROFILE '.platformio/penv/Scripts/python.exe')) { Join-Path $env:USERPROFILE '.platformio/penv/Scripts/python.exe' } `
              elseif (Get-Command python -ErrorAction SilentlyContinue) { (Get-Command python).Source } `
              else { 'python' }
$previousBuild = $env:PLATFORMIO_BUILD_DIR
$previousSerial = $env:RACOOL_SERIAL_PORT
$previousChanges = $env:RACOOL_REPORT_CHANGES
$previousTraffic = $env:RACOOL_REPORT_TRAFFIC
function Assert-CleanLog([string]$Path) {
    $diagnostics = Select-String -Path $Path -Pattern '\b(?:warning|error)\s*:|CMake (?:Deprecation )?Warning|\bWarning[!:]|\b[A-Za-z]+Warning:'
    if ($diagnostics) { throw "Build/upload diagnostics in $Path" }
}
try {
    $env:PLATFORMIO_BUILD_DIR = 'test/build/health_firmware'
    & $taskPython -m platformio run -e esp32-c6-health-report -t upload --upload-port $Port -j 4 *> test/build/upload-health-report.log
    if ($LASTEXITCODE -ne 0) { throw 'Diagnostic upload failed' }
    Assert-CleanLog 'test/build/upload-health-report.log'
    $env:RACOOL_SERIAL_PORT = $Port
    $env:RACOOL_REPORT_CHANGES = '1'
    $env:RACOOL_REPORT_TRAFFIC = '1'
    & node test/Communication/Zigbee/diagnostic/live_zigbee.mjs verify rapor_sikligi_saglik_s *> test/build/health-report-live.log
    if ($LASTEXITCODE -ne 0) { throw 'Health reporting verification failed' }
    Copy-Item -LiteralPath test/build/zigbee-live-verify.json -Destination test/build/health-report-live.json
} finally {
    try {
        $env:PLATFORMIO_BUILD_DIR = 'test/build/firmware_verification'
        & $taskPython -m platformio run -e esp32-c6-devkitc-1 -t upload --upload-port $Port -j 4 *> test/build/health-report-production-restore.log
        if ($LASTEXITCODE -ne 0) { throw 'Production restoration failed' }
        Assert-CleanLog 'test/build/health-report-production-restore.log'
    } finally {
        $env:PLATFORMIO_BUILD_DIR = $previousBuild
        $env:RACOOL_SERIAL_PORT = $previousSerial
        $env:RACOOL_REPORT_CHANGES = $previousChanges
        $env:RACOOL_REPORT_TRAFFIC = $previousTraffic
    }
}
