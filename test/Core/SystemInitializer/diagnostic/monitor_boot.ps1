param([string]$Port = $env:RACOOL_SERIAL_PORT, [string]$Output = 'test/build/boot-monitor.log', [switch]$HardwareReset)
$ErrorActionPreference = 'Stop'
if (!$Port) { throw 'Pass -Port or set RACOOL_SERIAL_PORT to the enumerated USB device.' }
$projectRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))
$python = if ($env:RACOOL_PYTHON) { $env:RACOOL_PYTHON } else { Join-Path $projectRoot '.venv/Scripts/python.exe' }
$arguments = @((Join-Path $projectRoot 'test/Communication/DebugConsole/diagnostic/serial_capture.py'), '--port', $Port, '--duration', '25', '--output', $Output)
if ($HardwareReset) { $arguments += '--hardware-reset' }
& $python @arguments
exit $LASTEXITCODE
