# Exercise failure and restore flow without invoking PlatformIO or hardware.
$ErrorActionPreference = 'Stop'
$testRoot = Split-Path -Parent $PSScriptRoot
$projectRoot = Split-Path -Parent $testRoot
$harnessRoot = Join-Path $projectRoot ('test/build/target-wrapper-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path (Join-Path $harnessRoot 'test/build') | Out-Null
Copy-Item (Join-Path $testRoot 'run_target_tests.ps1') (Join-Path $harnessRoot 'test/run_target_tests.ps1')
$harnessWrapper = Join-Path $harnessRoot 'test/run_target_tests.ps1'
$wrapperLines = Get-Content -LiteralPath $harnessWrapper
@(
    $wrapperLines[0]
    "[System.Threading.Thread]::CurrentThread.CurrentCulture = [System.Globalization.CultureInfo]::GetCultureInfo('tr-TR')"
    $wrapperLines[1..($wrapperLines.Count - 1)]
) | Set-Content -LiteralPath $harnessWrapper

'process.exit(0)' | Set-Content (Join-Path $harnessRoot 'test/check_test_inventory.mjs')
'process.exit(0)' | Set-Content (Join-Path $harnessRoot 'test/check_target_results.mjs')
@'
import sys, os
scenario = os.environ.get("RACOOL_WRAPPER_CASE", "error")
print('ARGS=' + repr(sys.argv))
if 'test' in sys.argv:
    if scenario == 'error':
        print('FIRST_COMPILER_ERROR', file=sys.stderr)
        print('SECOND_COMPILER_ERROR', file=sys.stderr)
        sys.exit(7)
    if scenario == 'test_warning': print('compiler: warning: TEST_WARNING', file=sys.stderr)
    if scenario == 'lvgl_warning': print('[Warn]\t(0.000, +0)\t lv_init: already initialized')
    if scenario == 'clean': print('I (123) ConfigMgr: CPU Warning   : 80.0 C')
    sys.exit(0)
print('RESTORE_CONFIRMED')
if scenario == 'restore_warning': print('WARNING: RESTORE_WARNING', file=sys.stderr)
sys.exit(0)
'@ | Set-Content (Join-Path $harnessRoot 'platformio.py')
$previousScenario = $env:RACOOL_WRAPPER_CASE
$env:RACOOL_WRAPPER_CASE = 'error'
$logPath = Join-Path $harnessRoot 'result.log'
# This child intentionally exits nonzero; collect all diagnostic lines.
$ErrorActionPreference = 'Continue'
powershell.exe -ExecutionPolicy Bypass -File (Join-Path $harnessRoot 'test/run_target_tests.ps1') -PlatformioPython (Join-Path $projectRoot '.venv/Scripts/python.exe') -Port 'TEST_USB_PORT' *> $logPath
$result = $LASTEXITCODE
$ErrorActionPreference = 'Stop'
$text = Get-Content -LiteralPath $logPath -Raw
if ($result -ne 1 -or $text -notmatch 'SECOND_COMPILER_ERROR' -or $text -notmatch 'RESTORE_CONFIRMED') {
    throw "Target wrapper failed acceptance; see $logPath"
}
if (($text | Select-String -Pattern "'--upload-port', 'TEST_USB_PORT'" -AllMatches).Matches.Count -ne 2 -or
    $text -notmatch "'--test-port', 'TEST_USB_PORT'") {
    throw "Selected USB port was not forwarded to testing and restoration; see $logPath"
}
if ($text -notmatch "'test', '-vv'") {
    throw "Target build/upload output is still silent; see $logPath"
}
Write-Output 'PASS: complete native stderr, visible build/upload output, failure exit, restoration path'
try {
    foreach ($scenario in @('test_warning', 'lvgl_warning', 'restore_warning', 'clean')) {
        $env:RACOOL_WRAPPER_CASE = $scenario
        $scenarioLog = Join-Path $harnessRoot ($scenario + '.log')
        $ErrorActionPreference = 'Continue'
        powershell.exe -ExecutionPolicy Bypass -File (Join-Path $harnessRoot 'test/run_target_tests.ps1') -PlatformioPython (Join-Path $projectRoot '.venv/Scripts/python.exe') -Port 'TEST_USB_PORT' *> $scenarioLog
        $scenarioExit = $LASTEXITCODE
        $ErrorActionPreference = 'Stop'
        $expectedExit = if ($scenario -eq 'clean') { 0 } else { 1 }
        $scenarioText = Get-Content -LiteralPath $scenarioLog -Raw
        if ($scenarioExit -ne $expectedExit -or $scenarioText -notmatch 'RESTORE_CONFIRMED') {
            throw "Warning/clean acceptance failed for $scenario (exit=$scenarioExit expected=$expectedExit); see $scenarioLog"
        }
        if ($scenario -ne 'clean' -and $scenarioText -notmatch 'zero-warning/error acceptance') {
            throw "Diagnostic rejection was not recorded for $scenario; see $scenarioLog"
        }
    }
} finally {
    $env:RACOOL_WRAPPER_CASE = $previousScenario
}
Write-Output 'PASS: test warnings rejected, restoration warnings rejected, clean success accepted'
