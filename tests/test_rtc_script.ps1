$ErrorActionPreference = 'Stop'
class RtcTestPort {
    [bool]$DtrEnable; [bool]$RtsEnable; [object]$Handshake
    [string]$NewLine; [int]$ReadTimeout; [int]$WriteTimeout; [bool]$IsOpen
    [string]$Pending=''; [string]$Input=''; [int]$Sets; [int]$Syncs; [int]$Mode
    [void]Open() {$this.IsOpen=$true}
    [void]Close() {$this.IsOpen=$false}
    [void]Dispose() {}
    [void]DiscardInBuffer() {$this.Pending=''}
    [void]Write([string]$c) {
        if ($c -ne "`n") {$this.Input+=$c;return}
        $line=$this.Input; $this.Input=''
        if ($line -eq ([char]24).ToString()) {
            $this.Pending+="RTC: REJECTED LINE_ERROR`r`n"
        } elseif ($line -eq 'RTC SYNC') {
            $this.Syncs++
            if ($this.Mode -eq 2) {$this.Pending+="RTC: RX_ERROR flags=00000010`r`n"}
            else {$this.Pending+="RTC: SYNC_OK`r`n"}
        } elseif ($line.StartsWith('RTC SET ')) {
            $this.Sets++;$date=$line.Substring(8)
            if ($this.Mode -eq 1) {$this.Pending+="RTC: RX_ERROR flags=00000100`r`n"}
            else {$this.Pending+="RTC: SET_ACCEPTED $date`r`nRTC: SET_OK verified=1 osf=0`r`nRTC: TIME=$date trusted=1 health=0`r`n"}
        }
    }
    [string]ReadExisting() {$s=$this.Pending;$this.Pending='';return $s}
}
$source=Get-Content "$PSScriptRoot/../tools/Set-RtcTime.ps1" -Raw
$constructor='[System.IO.Ports.SerialPort]::new($Port,115200,[System.IO.Ports.Parity]::None,8,[System.IO.Ports.StopBits]::One)'
if (-not $source.Contains($constructor)) {throw 'Test serial injection point not found'}
$program=[scriptblock]::Create($source.Replace($constructor,'$script:TestRtcPort'))
foreach ($mode in 0,1,2) {
    $script:TestRtcPort=[RtcTestPort]::new();$script:TestRtcPort.Mode=$mode
    $failure=$null
    try {& $program -Port TEST -TimeoutSeconds 1 -ByteDelayMs 0} catch {$failure=$_}
    if ($mode -eq 0 -and ($failure -or $TestRtcPort.Sets -ne 1)) {throw "Handshake/verification failed: $failure"}
    if ($mode -eq 1 -and (-not $failure -or $TestRtcPort.Sets -ne 1)) {throw 'SET RX error must fail without retrying write'}
    if ($mode -eq 2 -and (-not $failure -or $TestRtcPort.Sets -ne 0 -or $TestRtcPort.Syncs -ne 3)) {throw 'No SYNC must prevent SET'}
    if ($TestRtcPort.IsOpen) {throw 'Serial port was not closed'}
}
'PASS script delayed prelude rejection, acknowledged SYNC, verified SET, RX failure, bounded read-only retries, port cleanup'
