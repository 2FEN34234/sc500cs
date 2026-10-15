param([string]$Cxx = 'g++', [switch]$Integration)
$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
$build = Join-Path $root 'build'
New-Item -ItemType Directory -Force -Path $build | Out-Null
$exe = Join-Path $build 'protocol_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 (Join-Path $root 'tests\protocol_test.cpp') -o $exe
if ($LASTEXITCODE -ne 0) { throw 'Protocol host compilation failed' }
& $exe
if ($LASTEXITCODE -ne 0) { throw 'Protocol host tests failed' }
$json = Join-Path $root '.tools\user\libraries\ArduinoJson\src'
if (-not (Test-Path -LiteralPath (Join-Path $json 'ArduinoJson.h'))) { throw 'Install pinned ArduinoJson with build.ps1 -Bootstrap first' }
$app = Join-Path $build 'app_test.exe'
& $Cxx -std=c++17 -Wall -Wextra -Werror -O2 '-I' (Join-Path $root 'tests\fakes') '-I' $json (Join-Path $root 'tests\app_test.cpp') -o $app
if ($LASTEXITCODE -ne 0) { throw 'Firmware state host compilation failed' }
& $app
if ($LASTEXITCODE -ne 0) { throw 'Firmware state host tests failed' }
if ($Integration) {
    $object = Join-Path $build 'mcu_adapter.o'
    & gcc -std=c11 -Wall -Wextra -Werror -O2 -c (Join-Path $root 'tests\mcu_adapter.c') -o $object
    if ($LASTEXITCODE -ne 0) { throw 'MCU transport adapter compilation failed' }
    $driver = Join-Path $build 'integration_driver.exe'
    & $Cxx -std=c++17 -Wall -Wextra -Werror -O2 '-I' (Join-Path $root 'tests\fakes') '-I' $json (Join-Path $root 'tests\integration_driver.cpp') $object -o $driver
    if ($LASTEXITCODE -ne 0) { throw 'Integration driver compilation failed' }
    & python (Join-Path $root 'tests\test_integration.py')
    if ($LASTEXITCODE -ne 0) { throw 'Firmware/MCU/server integration failed' }
}
