param(
    [string]$Cli = '',
    [switch]$Bootstrap,
    [string]$Fqbn = 'esp8266:esp8266:generic:xtal=160,eesz=1M,FlashMode=dout'
)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
if (-not $Cli) {
    $found = Get-Command arduino-cli -ErrorAction SilentlyContinue
    if ($found) { $Cli = $found.Source }
    else { $Cli = Join-Path $env:LOCALAPPDATA 'Programs\Arduino IDE\resources\app\lib\backend\resources\arduino-cli.exe' }
}
if (-not (Test-Path -LiteralPath $Cli)) { throw 'Provide -Cli with an installed arduino-cli executable' }
$toolsDir = Join-Path $root '.tools'
$buildDir = Join-Path $root 'build'
foreach ($relative in @('.tools\data','.tools\downloads','.tools\user','build')) {
    New-Item -ItemType Directory -Force -Path (Join-Path $root $relative) | Out-Null
}
$config = Join-Path $toolsDir 'arduino-cli.yaml'
$data = (Join-Path $toolsDir 'data').Replace('\','/') | ConvertTo-Json -Compress
$downloads = (Join-Path $toolsDir 'downloads').Replace('\','/') | ConvertTo-Json -Compress
$userDir = (Join-Path $toolsDir 'user').Replace('\','/') | ConvertTo-Json -Compress
@"
board_manager:
  additional_urls:
    - https://arduino.esp8266.com/stable/package_esp8266com_index.json
directories:
  data: $data
  downloads: $downloads
  user: $userDir
"@ | Set-Content -LiteralPath $config -Encoding utf8
if ($Bootstrap) {
    & $Cli --config-file $config core update-index
    if ($LASTEXITCODE -ne 0) { throw 'Arduino index download failed' }
    & python (Join-Path $root 'tools\fetch_core.py')
    if ($LASTEXITCODE -ne 0) { throw 'Pinned core download failed' }
    & $Cli --config-file $config core install esp8266:esp8266@3.1.2
    if ($LASTEXITCODE -ne 0) { throw 'ESP8266 core installation failed' }
    & $Cli --config-file $config lib install ArduinoJson@6.21.5
    if ($LASTEXITCODE -ne 0) { throw 'ArduinoJson installation failed' }
}
# Only compile: this script has no port argument, reset operation or upload command.
& $Cli version | Tee-Object -FilePath (Join-Path $buildDir 'tool-version.txt')
$Fqbn | Set-Content -LiteralPath (Join-Path $buildDir 'fqbn.txt') -Encoding utf8
& $Cli --config-file $config compile --fqbn $Fqbn --warnings all --build-path (Join-Path $buildDir 'firmware') --output-dir (Join-Path $buildDir 'artifacts') (Join-Path $root 'device') 2>&1 | Tee-Object -FilePath (Join-Path $buildDir 'compile.log')
if ($LASTEXITCODE -ne 0) { throw 'ESP8266 compilation failed; inspect build/compile.log' }
Get-FileHash -Algorithm SHA256 -LiteralPath (Join-Path $buildDir 'artifacts\device.ino.bin') | Format-List
