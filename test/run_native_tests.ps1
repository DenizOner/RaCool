# Run every registered host-native suite, or select suites by name.
param([string]$Filter = "")
$ErrorActionPreference = 'Stop'
$RootDir = Split-Path -Parent (Split-Path -Parent $PSCommandPath)
$NodeCommand = Get-Command node -CommandType Application -ErrorAction Stop
& $NodeCommand.Source "$RootDir/test/run_native_tests.mjs" $Filter
exit $LASTEXITCODE
