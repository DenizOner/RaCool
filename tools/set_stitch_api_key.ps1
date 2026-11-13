# Enter a key without exposing it to terminal output, command history or source files.
$ErrorActionPreference = 'Stop'
$secureKey = Read-Host 'Stitch API key' -AsSecureString
$keyPointer = [IntPtr]::Zero
try {
    $keyPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureKey)
    $stitchKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($keyPointer).Trim()
    if ([string]::IsNullOrWhiteSpace($stitchKey) -or $stitchKey.Contains("`r") -or $stitchKey.Contains("`n")) {
        throw 'A nonempty, single-line API key is required.'
    }
    [Environment]::SetEnvironmentVariable('STITCH_API_KEY', $stitchKey, 'User')
    $env:STITCH_API_KEY = $stitchKey
    Write-Output 'STITCH_API_KEY saved for this Windows user. Close and reopen VS Code/Codex so they inherit it.'
} finally {
    $stitchKey = $null
    if ($keyPointer -ne [IntPtr]::Zero) { [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($keyPointer) }
    $secureKey.Dispose()
}
