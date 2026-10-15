镜头：远程相册和设备控制服务器（单设备 v1）

这份目录可以独立复制到 Linux Docker 主机。它提供后端、手机网页和 Caddy HTTPS。
真实 ESP8266 固件和 MCU USART0 桥由另外的工作接入；安装服务器本身不会让
现有 FPGA 自动联网。首次打开网页显示“设备离线、暂无照片”是正常状态。
协议基线：../ESP8266_INTERFACE_PROPOSAL.txt + ../INTERFACE_REVIEW.txt 的修订，
另见 ../MCU_PROTOCOL_ADDENDUM.txt：服务器持久分配递增的 command_id，
使 MCU 的有限结果缓存不导致旧命令重执行。部署本次 MCU 时请同步更新服务器。

一、安装到 Linux
1. 把本目录完整复制到服务器，例如 ~/camera-server。无需复制 FPGA 工程。
   Windows PowerShell 示例（将 USER 换成你的 SSH 用户名）：
   scp -r D:/2026fpga/lab_hd_1_mipi_hdmi/wifi/server USER@101.43.33.186:~/camera-server

2. 在服务器终端执行：
   cd ~/camera-server
   python3 init_config.py --host YOUR_DNS_HOST
   docker compose config --quiet
   docker compose up -d --build
   docker compose ps

   init_config.py 只在 .env 不存在时生成随机凭据，不会覆盖旧配置。
   若系统没有 python3，可使用 Docker 生成配置：
   docker run --rm -v "$PWD:/work" -w /work python:3.12-slim python init_config.py --host YOUR_DNS_HOST
   保留 .env 和 Docker volumes，升级时不要重新生成设备身份。

3. 云安全组和服务器防火墙允许入站 TCP 80/443；SSH 保持原设置。
   后端 8000 不映射到公网；公网只暴露 Caddy。

二、首次 HTTPS 访问（ESP 必须使用 DNS 名）
先将自己的 DNS 名解析到 101.43.33.186，再将 YOUR_DNS_HOST 替换成该名字。
根据 Core 3.1.2 打包 BearSSL 的原生 brssl 证书验证实测，部署必须采用 DNS 身份，
不使用 https://101.43.33.186 作为 ESP 地址。服务器 IP 不变，证书须匹配 DNS 名。
纯 IP SAN 返回 BAD_SERVER_NAME(56)，匹配 DNS SAN 通过；错误 host/CA/过期均被拒绝。
证据见 ../esp8266/build/tls-test.log，复现脚本 ../esp8266/test_tls.ps1。
ESP 模板中的 camera.example.invalid 是占位符，部署时必须换成解析至上述 IP 的真实域名。
实际板端握手仍待测试，不允许跳过证书验证。
Caddyfile 使用 tls internal，自建 CA，为 SITE_ADDRESS 签发匹配的证书。
已有 .env 不会被生成器覆盖：需手动将旧 SITE_ADDRESS=101.43.33.186 改成 DNS 名。
浏览器默认不信任该 CA，这是未安装信任证书导致的，不是关闭 TLS 的理由。
导出 CA 公钥证书：
   docker compose cp caddy:/data/caddy/pki/authorities/local/root.crt ./camera-root-ca.crt

通过可信的 SSH/SCP 通道取得这一个 root.crt，并在自己手机/电脑的信任设置中
安装；具体菜单依手机系统而异（iOS 安装描述文件后还需启用完全信任）。
不要导出或分享 root.key、intermediate.key；不要让 ESP 使用 setInsecure。
仅为自己的服务器安装此 CA，它有权为该设备签发其他受信任证书。

服务器验证示例（此命令不输出凭据）：
   curl --cacert camera-root-ca.crt https://YOUR_DNS_HOST/healthz
预期 {"ok":true}。

打开网页时浏览器会要求登录，用户名和密码在本机 .env 中：
   CAMERA_ADMIN_USER / CAMERA_ADMIN_PASSWORD
设备使用不同的 CAMERA_DEVICE_ID / CAMERA_DEVICE_TOKEN，不把设备密钥给网页。
Basic 登录由浏览器管理；使用共享设备时关闭私人浏览窗口结束该登录会话。

若要改用公开 CA：保持 DNS 指向公网 IP，移除 Caddyfile
中的 tls internal，再 docker compose up -d --force-recreate caddy。
域名证书可由 Caddy 自动申请。ESP 固件也必须更新目标地址和信任配置。
实际 ESP 的 TLS 握手和峰值内存仍需板端验证；不能绕过 CA 或主机名验证。

三、设备接入
- base_url: https://YOUR_DNS_HOST（与 SITE_ADDRESS、证书 DNS SAN 一致）
- device_id/token: .env 中生成值，分别填入 Arduino 本地忽略配置。
- CA: 上述 root.crt（公钥）。ESP 首次 TLS 前须有合理时间，支持证书有效期校验。
- 30 秒心跳，90 秒未见心跳网页显示离线；MCU 在线状态独立显示。
- GET /api/v1/devices/{device_id}/time 返回 UTC Unix 秒与 rtc_utc_offset_min=480。
- 所有设备接口都需要 Authorization: Bearer <token>。
- JSON 最多 2048 字节，照片块最多 512 字节。响应 JSON 带 Content-Length。
- 网络命令必须由 MCU 确认后才回报执行成功；server 不伪造任何硬件状态。

四、照片上传
POST .../photos 注册不可变 photo_id、文件名、实长、CRC32 等元数据。
GET .../photos/{photo_id} 可查询当前 next_offset。
PUT .../photos/{photo_id}/chunks/{offset} 上传 1..512 字节，Content-Type 为
application/octet-stream。收到持久化确认后才推进；相同块可幂等重试。
POST .../photos/{photo_id}/complete，JSON {}，校验总长度、CRC 和 BMP 结构。
完整照片才显示在相册。首版支持现有未压缩 24 位 BMP（40 字节 DIB、54 字节头）。
整图 CRC 使用 CRC-32/ISO-HDLC，例如 123456789 -> cbf43926。
设备只在服务器 complete=true 且 ID/长度/CRC 匹配后标记上传成功，不删除 SD 原文件。

五、控制与失败语义
网页提供单张拍照、延时摄影开始/停止、曝光(3..3990)/增益(16..143)。
间隔选择 2/5/10/30/60 秒，但实际拍照周期受设备保存时间限制。
每设备最多一个待执行命令，设备或 MCU 离线时拒绝创建。
queued -> claimed -> dispatched -> succeeded/failed/unknown。
租约 120 秒，设备 active 事件续租；掉线/重启后的未知结果不会自动重放。
queued 超过 120 秒显示 expired，领取后租约过期显示 unknown。
再次操作会生成一个新动作，unknown 时先核对设备/SD，再决定是否重新拍照。
领取响应丢失时，未到期 claimed 请求返回相同 command_id/lease_id；设备仍需去重。
MCU 持久去重尚未落地，不能宣称任意断电情况下 exactly-once。

六、存储和维护
camera_data volume：SQLite、待传 .part 和完成 .bmp；默认预留配额 2GiB。
配额包含未完成上传，达到上限返回 507，不自动删除任何照片。
首版没有网页删除/清理按钮；长期使用需监控磁盘与配额，后续可加入明确的清理操作。
上传文件按 UUID 命名，客户端文件名不参与服务器路径拼接。
caddy_data 保存 CA 私钥和证书；保留该卷，否则手机/ESP 的 CA 信任需要更新。
不要运行 docker compose down -v，除非明确要删除所有照片、数据库和证书。
升级：保存 .env 和 volumes，在目录执行 docker compose up -d --build。
日志：docker compose logs --tail 100 api caddy（服务不记录令牌/请求体）。
停机备份可对 Docker volumes 做完整备份；SQLite 要连同 WAL 一致备份，或先停 api。

七、本地验证
   python -m pip install -r requirements-test.txt
   python -m unittest discover -s tests -v

测试使用临时目录和模拟设备，不会访问真实服务器、不烧写硬件。
Docker 容器固定一个 Uvicorn worker；本实现使用进程内文件锁，不能直接增加 worker
或水平扩容。设备规模扩大时需改用跨进程锁、独立对象存储和数据库事务设计。
手机相册直接展示 BMP，下载是原始 BMP；目前没有 JPEG 缩略图或实时视频功能。

参考文档
https://caddyserver.com/docs/automatic-https
https://fastapi.tiangolo.com/advanced/security/http-basic-auth/
https://docs.docker.com/compose/how-tos/environment-variables/set-environment-variables/
