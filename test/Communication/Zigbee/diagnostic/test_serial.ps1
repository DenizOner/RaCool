$port = [System.IO.Ports.SerialPort]::new("COM5", 115200, [System.IO.Ports.Parity]::None, 8, [System.IO.Ports.StopBits]::One)
$port.DtrEnable = $false
$port.RtsEnable = $false
$port.ReadTimeout = 2000
$port.Open()
Start-Sleep -Milliseconds 300
$existing = $port.ReadExisting()
if ($existing) {
    Write-Host "--- Initial Output ---"
    Write-Host $existing
}

Write-Host "--- Sending 'ZB DIAG' ---"
$port.WriteLine("ZB DIAG")
Start-Sleep -Milliseconds 1000

Write-Host "--- Sending 'ZB STEER' ---"
$port.WriteLine("ZB STEER")
Start-Sleep -Milliseconds 2000

$response = ""
try {
    while ($true) {
        $line = $port.ReadLine()
        Write-Host $line
    }
} catch {
    # timeout reached
}
$port.Close()
