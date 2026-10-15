$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$bear = Join-Path $root '.tools\data\packages\esp8266\hardware\esp8266\3.1.2\tools\sdk\ssl\bearssl'
if (-not (Test-Path -LiteralPath $bear)) { throw 'Run build.ps1 -Bootstrap first' }
$include = (Join-Path $root 'tests\bearssl_host').Replace('\','/')
New-Item -ItemType Directory -Force -Path (Join-Path $bear 'build_native\obj') | Out-Null
Push-Location $bear
try {
    & mingw32-make -j4 CONF=Unix CC=gcc BUILD=build_native E=.exe "CFLAGS=-W -Wall -Os -DBR_SLOW_MUL15=1 -include winsock2.h -I$include" build_native/libbearssl.a *> (Join-Path $root 'build\bearssl-native.log')
    if ($LASTEXITCODE -ne 0) { throw 'Native BearSSL build failed; inspect build/bearssl-native.log' }
    $sources = Get-ChildItem -LiteralPath (Join-Path $bear 'tools') -Filter '*.c' | ForEach-Object { $_.FullName }
    & gcc -Os -include winsock2.h "-I$include" -Iinc -Isrc @sources build_native/libbearssl.a -lws2_32 -ladvapi32 -o build_native/brssl.exe >> (Join-Path $root 'build\bearssl-native.log') 2>&1
    if ($LASTEXITCODE -ne 0) { throw 'Native BearSSL verification tool link failed' }
} finally { Pop-Location }
& python (Join-Path $root 'tests\test_tls.py') (Join-Path $bear 'build_native\brssl.exe') 2>&1 | Tee-Object -FilePath (Join-Path $root 'build\tls-test.log')
if ($LASTEXITCODE -ne 0) { throw 'Certificate validation tests failed' }
