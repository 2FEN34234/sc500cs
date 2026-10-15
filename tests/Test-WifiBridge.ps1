$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$gcc='E:/mingw64/bin/gcc.exe'
$gxx='E:/mingw64/bin/g++.exe'
$ll='FDwork/anlu/ph1p35_fpsoc_cpu0/driver/ps_driver/usart_n/inc'
$log='FDwork/anlu/ph1p35_fpsoc_cpu0/lib/log'
function Check-Native([string]$name) { if ($LASTEXITCODE -ne 0) {throw "$name failed ($LASTEXITCODE)"} }
Push-Location $repo
try {
    foreach($test in @('wifi_transport','wifi_mcu','record_menu','picture_menu','touch_focus')) {
        & $gcc -std=c11 -O2 "tests/test_$test.c" -o "build/test_$test.exe"
        Check-Native "$test compile"
        & "./build/test_$test.exe"
        Check-Native $test
    }
    & $gcc -std=c11 -O2 -Itests/wifi_uart_host -Itests/rtc_uart_host "-I$ll" tests/test_wifi_uart.c -o build/test_wifi_uart.exe
    Check-Native 'wifi UART compile'
    & ./build/test_wifi_uart.exe
    Check-Native 'wifi UART'
    & $gxx -std=c++17 -O2 tests/test_wifi_interop.cpp -o build/test_wifi_interop.exe
    Check-Native 'interop compile'
    & ./build/test_wifi_interop.exe
    Check-Native 'interop'
    & $gcc -std=c11 -O2 -Itests/recorder_host tests/test_rtc_diag.c -o build/test_rtc_diag.exe
    Check-Native 'RTC compile'
    & ./build/test_rtc_diag.exe
    Check-Native 'RTC'
    & $gcc -std=c11 -O2 -Itests/rtc_uart_host "-I$ll" tests/test_rtc_uart.c -o build/test_rtc_uart.exe
    Check-Native 'RTC UART compile'
    & ./build/test_rtc_uart.exe
    Check-Native 'RTC UART'
    & $gcc -std=c11 -O2 -Itests/recorder_host tests/test_sd_recording.c FDwork/shared/fatfs/ff.c -o build/test_sd_recording.exe
    Check-Native 'SD compile'
    & ./build/test_sd_recording.exe *> build/wifi_sd_test.log
    Check-Native 'SD'
    Get-Content build/wifi_sd_test.log -Tail 1
    & $gcc -std=c11 -O2 -DAL_SIMP_NO_FLOAT -Itests/printf_host "-I$log/inc" tests/test_compact_printf.c "$log/src/al_printf.c" "$log/src/al_simp_printf.c" -o build/test_compact_printf.exe
    Check-Native 'formatter compile'
    & ./build/test_compact_printf.exe
    Check-Native 'formatter'
} finally {Pop-Location}
