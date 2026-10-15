#pragma once
// Copy to config.local.h and fill privately. Default example compiles but never connects.
#define CAMERA_CONFIGURED 0
#define CAMERA_WIFI_SSID ""
#define CAMERA_WIFI_PASSWORD ""
#define CAMERA_DEVICE_ID "00000000000000000000000000000000"
#define CAMERA_DEVICE_TOKEN ""
// Host only, no scheme/path. DNS name must resolve to 101.43.33.186 and match SAN.
// Core 3.1.2 BearSSL rejects IP-only SAN certificates; see test_tls.ps1.
#define CAMERA_SERVER_HOST "camera.example.invalid"
#define CAMERA_SERVER_PORT 443
#define CAMERA_NTP_1 "ntp.aliyun.com"
#define CAMERA_NTP_2 "pool.ntp.org"
#define CAMERA_FIRMWARE_VERSION "0.1.0"
