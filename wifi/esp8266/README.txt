ESP8266 Arduino 固件 / 2026-10-14

用途及边界
适用 ATK-ESP-01 / ESP8266，连接 MCU USART0，再主动连接服务器。
协议：../ESP8266_INTERFACE_PROPOSAL.txt + ../INTERFACE_REVIEW.txt，后者优先。
MCU、FPGA 和服务器由原对话负责；本目录不改变既有工程、启动方式或下载产物。
本固件不包含 AT 命令兼容层、OTA、Web 配网或删除 SD 照片功能。

源码
device/app.cpp：Wi-Fi/SNTP/HTTPS、UART RPC、命令状态、RTC 与照片上传。
device/protocol.h：可移植 UART v1 编解码、校验和业务边界检查。
device/device.ino：Arduino 入口；UART0 始终仅二进制协议，不输出调试文本。
USART1/COM9 是 MCU PC 日志口，不是 ESP 烧录口。

本地配置
1. 复制 device/config.example.h 为 device/config.local.h。
2. 填入 Wi-Fi SSID/密码、32位小写hex设备ID、设备令牌、证书匹配的服务器host。
3. 复制 device/ca.example.h 为 device/ca.local.h，粘贴部署导出的 CA 公钥证书。
4. 将 CAMERA_CONFIGURED 设为 1。不要提交两个 local.h，不要打印密码或令牌。
   .gitignore 已忽略本地配置、工具链和构建产物（产物也可能含密钥）。
未配置时默认能编译，但关闭 Wi-Fi，既不联网也不向 MCU 发业务命令。
不能将这个模板产物当作已配置生产固件。
CA/目标身份/日期验证失败时保持断开；不降级 HTTP，不使用 setInsecure。
初始 SNTP 仅供 TLS 时间校验；RTC 使用已验证 HTTPS /time 的 UTC epoch+480。
已用 Core 3.1.2 自带 BearSSL 源码做本机证书验证：纯 IP SAN 被拒绝，
匹配 DNS SAN 通过；因此使用可解析至 101.43.33.186 的 DNS 名称，
证书 SAN 必须包含该名称。模板 camera.example.invalid 故意不可用，需替换。
绝不能只信任 CA 后跳过主机名校验；部署证书和真实ESP TLS握手仍需实测。

独立构建（PowerShell，在本目录）
  .\build.ps1 -Bootstrap
之后重复构建：
  .\build.ps1
如 arduino-cli 不在默认安装位置：
  .\build.ps1 -Cli '实际路径\arduino-cli.exe' -Bootstrap
脚本仅使用本目录 .tools/ 的 Arduino 数据、库、下载缓存，不覆盖全局 Arduino 配置。
锁定 ESP8266 Core 3.1.2、ArduinoJson 6.21.5；本次 CLI 为 1.5.1。
默认 FQBN：esp8266:esp8266:generic:xtal=160,eesz=1M,FlashMode=dout。
1MB 是离线编译假设，尚未检测实际模块 Flash；160MHz、DOUT 也需板端验证。
实际 Flash 确认后使用 -Fqbn 指定；不要把默认 1MB 当作硬件已核实。
build/artifacts/device.ino.bin 是本工程独立产物，绝不覆盖 FPGA/MCU 产物。
build/compile.log 保存真实编译与 RAM/IRAM/Flash 统计。

测试（不打开任何 COM 口，不连接公网服务器）
  .\test.ps1
需要 MinGW g++；测试实际 protocol.h 和 app.cpp，HTTP/UART 使用可控替身。
三方本地联测：
  .\test.ps1 -Integration
另需 gcc、Python 及 ../server/requirements-test.txt 的依赖。
联测把原对话的 FDwork/shared/wifi_transport.h 编译为 C，再与实际 ESP 状态代码
链接，Python 转发请求给真实 FastAPI 服务 TestClient，使用临时目录和测试凭据。
相机、SD物理操作、UART电气层、Wi-Fi、TLS socket 是模拟的；未表示真实上板成功。
注入服务器已保存但设备丢失事件/块应答，确认幂等恢复、一次拍照、一次上传提交。
测试照片名 SIM_TEST.BMP，不能当作摄像头真实拍摄结果。
证书逻辑测试（另需 mingw32-make、gcc、Python cryptography）：
  .\test_tls.ps1
从锁定 Core 自带的 BearSSL 源码编译本机验证工具；仅生成临时测试公钥证书。
验证 IP SAN拒绝、DNS SAN通过、错误host/CA/过期证书拒绝，不连接任何服务器。

运行行为与恢复
- 115200/8N1；COBS+CRC32，单个请求最多首次+3次相同序号发送；普通应答1秒。
- 每5秒 HELLO/STATUS。boot_id变化或 UART 故障使在途命令 unknown，不重放动作。
- 心跳30秒，命令空闲轮询2秒；必须心跳成功且 MCU 在线/有能力才下发动作。
- COMMAND_ACK 只产生 dispatched，最终状态必须来自 MCU RESULT。
- 网络失败重试同 event_id；服务器409/404使本地退休该命令，不重新执行。
- 设备重启生成新boot_id，由服务器使之前在途命令unknown；无跨重启exactly-once保证。
- RTC校时一小时一次，只有写后验证结果通过才记为已验证；超时不宣称RTC没写入。
- 照片每块最多512字节，仅在无UART应答等待时发HTTPS；不缓存整图。
- 块上传超时先查服务端offset；服务器完整确认ID/长度/CRC后才PHOTO_COMMIT。
- MCU保存照片上传标记，源照片不删除。ESP自身不将照片或传输偏移写Flash。
- CRC/图片格式错误、元数据冲突或507容量不足，暂停照片通道，继续心跳和命令。
  处理原因后重启ESP可重新查询SD与服务端，恢复上传；不会自动删除文件腾空间。
- 401/403停止网络业务直到重新配置/重启；Wi-Fi断线自动重连。
- 程序保留内部 fault 诊断值，禁止在UART0打印。当前服务器严格心跳结构没有
  ESP故障扩展字段，因此详细设备故障未通过心跳上报；联调时需要调试器或扩展诊断。

仅供将来烧录的流程（本次未执行）
必须先得到用户明确烧录指令、确认Flash容量，并备份原AT固件。
断开/隔离可能争用TX/RX的驱动，用3.3V逻辑USB-UART和足够电流的3.3V供电。
正常启动GPIO0上拉高；下载模式仅在复位采样时GPIO0低；RST/GPIO0默认释放。
不要把5V逻辑接到模块，不把MCU USART1 COM9当作ESP下载接口。
先确认真实下载口与复位方式，再按ESP8266/esptool文档执行只读flash_id、备份，
最后依据正确Flash参数烧录；本目录脚本故意不提供自动upload或复位动作。
本次未碰模块Flash，也未操作FPGA RST/GPIO0寄存器。

必须完成的硬件/部署验收
真实CA/身份校验（错误CA、错误host、过期证书必须拒绝）、SNTP无法获取时间、
Wi-Fi重连、TLS峰值堆/最长阻塞、串口溢出/噪声、MCU复位、SD断电恢复、
RTC读回和本地时区、实际640x360 BMP完整上传、服务器重启后恢复、照片原件保留。
不支持MFLN时保留默认TLS接收缓冲，不因JSON只有2048字节就缩小TLS record缓冲。

官方技术依据
https://arduino-esp8266.readthedocs.io/en/3.1.2/esp8266wifi/bearssl-client-secure-class.html
https://arduino-esp8266.readthedocs.io/en/3.1.2/installing.html
https://arduino.github.io/arduino-cli/1.3/commands/arduino-cli_compile/
