param(
    [string]$Port = 'COM9',
    [ValidateRange(1,60)][int]$TimeoutSeconds = 8,
    [ValidateRange(0,20)][int]$ByteDelayMs = 2,
    [switch]$DryRun
)
# USART1: 115200 8N1. Close VOFA/other serial tools before running this script.
# The DS3231 holds local wall time; no timezone is stored in its registers.
$ErrorActionPreference = 'Stop'
if ($DryRun) {
    'RTC SET ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss', [Globalization.CultureInfo]::InvariantCulture)
    return
}
$rtcPort = [System.IO.Ports.SerialPort]::new($Port,115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)
$rtcPort.DtrEnable = $false
$rtcPort.RtsEnable = $false
$rtcPort.Handshake = [System.IO.Ports.Handshake]::None
$rtcPort.NewLine = "`n"
$rtcPort.ReadTimeout = 300
$rtcPort.WriteTimeout = 1000
function Send-RtcLine([string]$Text) {
    # Calibration is infrequent. Pace bytes to allow RX servicing even while
    # other firmware work is busy; use -ByteDelayMs 0 for burst testing.
    foreach ($rtcChar in ($Text + "`n").ToCharArray()) {
        $rtcPort.Write($rtcChar.ToString())
        if ($ByteDelayMs) { Start-Sleep -Milliseconds $ByteDelayMs }
    }
}
try {
    $rtcPort.Open()
    $rtcPort.DiscardInBuffer()
    # Resynchronization is acknowledged, never a fixed-delay blind discard.
    # Only this non-mutating handshake is retried, never RTC SET.
    $rtcSynced = $false
    for ($rtcAttempt=1; $rtcAttempt -le 3 -and -not $rtcSynced; $rtcAttempt++) {
        Send-RtcLine (([char]24).ToString())
        Send-RtcLine 'RTC SYNC'
        $rtcBuffer = ''
        $rtcWatch = [Diagnostics.Stopwatch]::StartNew()
        while ($rtcWatch.Elapsed.TotalSeconds -lt $TimeoutSeconds -and -not $rtcSynced) {
            $rtcBuffer += $rtcPort.ReadExisting()
            while ($rtcBuffer.Contains("`n")) {
                $rtcSplit = $rtcBuffer.IndexOf("`n")
                $rtcLine = $rtcBuffer.Substring(0,$rtcSplit).TrimEnd("`r")
                $rtcBuffer = $rtcBuffer.Substring($rtcSplit+1)
                if ($rtcLine.Contains('RTC: RX_ERROR')) { Write-Host $rtcLine }
                if ($rtcLine -eq 'RTC: SYNC_OK') { $rtcSynced = $true }
            }
            if ($rtcBuffer.Length -gt 4096) { throw 'Serial response has no line ending.' }
            if (-not $rtcSynced) { Start-Sleep -Milliseconds 20 }
        }
    }
    if (-not $rtcSynced) { throw 'RTC SYNC failed; no time was sent. Install rtc-set-v3 firmware and check RX_ERROR flags / USART1 RX.' }
    $rtcCommand = 'RTC SET ' + (Get-Date).ToString('yyyy-MM-dd HH:mm:ss', [Globalization.CultureInfo]::InvariantCulture)
    Write-Host "Sending to ${Port}: $rtcCommand"
    Send-RtcLine $rtcCommand
    $rtcWatch = [Diagnostics.Stopwatch]::StartNew()
    $rtcBuffer = ''
    $rtcAccepted = $false
    $rtcVerified = $false
    while ($rtcWatch.Elapsed.TotalSeconds -lt $TimeoutSeconds) {
        $rtcBuffer += $rtcPort.ReadExisting()
        while ($rtcBuffer.Contains("`n")) {
            $rtcSplit = $rtcBuffer.IndexOf("`n")
            $rtcLine = $rtcBuffer.Substring(0,$rtcSplit).TrimEnd("`r")
            $rtcBuffer = $rtcBuffer.Substring($rtcSplit+1)
            if ($rtcLine.Contains('RTC:')) { Write-Host $rtcLine }
            if ($rtcLine.Contains('RTC: SET_ACCEPTED ' + $rtcCommand.Substring(8))) { $rtcAccepted = $true }
            if ($rtcLine -match 'RTC: (REJECTED|SET_FAILED|SET_TIMEOUT|RX_ERROR)') { throw "RTC setting failed: $rtcLine" }
            if ($rtcAccepted -and $rtcLine.Contains('RTC: SET_OK verified=1 osf=0')) { $rtcVerified = $true }
            if ($rtcVerified -and $rtcLine -match 'RTC: TIME=(\d{4}-\d{2}-\d{2} \d{2}:\d{2}:\d{2}) trusted=1') {
                $rtcReadback = [datetime]::ParseExact($Matches[1],'yyyy-MM-dd HH:mm:ss',[Globalization.CultureInfo]::InvariantCulture)
                if ([Math]::Abs(((Get-Date)-$rtcReadback).TotalSeconds) -gt 3) { throw 'RTC readback differs from computer time by more than 3 seconds.' }
                Write-Host 'RTC verified. The upper-right screen clock should now advance.'
                return
            }
        }
        if ($rtcBuffer.Length -gt 4096) { throw 'Serial response has no line ending.' }
        Start-Sleep -Milliseconds 20
    }
    throw 'No verified RTC response. Check FPGA RTC2 image, rtc-set-v3 firmware, COM port and USART1 RX wiring.'
} finally {
    if ($rtcPort.IsOpen) { $rtcPort.Close() }
    $rtcPort.Dispose()
}
