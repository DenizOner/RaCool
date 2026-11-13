$port = [System.IO.Ports.SerialPort]::new("COM5", 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$port.DtrEnable = $false
$port.RtsEnable = $false
$port.ReadTimeout = 1500
$port.Open()
Start-Sleep -Milliseconds 300

function Send-Cmd($cmd, $waitMs = 1000) {
    Write-Host ">>> COMMAND: $cmd"
    $port.WriteLine($cmd)
    Start-Sleep -Milliseconds $waitMs
    try {
        while ($port.BytesToRead -gt 0) {
            $line = $port.ReadLine()
            Write-Host $line
        }
    } catch {}
}

Send-Cmd "HELP" 800
Send-Cmd "bled state IDLE 3000" 1000
Send-Cmd "bled state HEATING 3000" 1000
Send-Cmd "bled state PAIRING_ACTIVE 3000" 1000
Send-Cmd "bled state CONNECTED 3000" 1000
Send-Cmd "bled state FAULT 3000" 1000
Send-Cmd "bled state ALARM 3000" 1000
Send-Cmd "rgb status" 800

$port.Close()
