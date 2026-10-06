# StopWatch 无线接收器 C480

硬件：立创实战派 ESP32-S3，N16R8，出厂 MAC `9C:13:9E:8A:C4:80`。
配套 StopWatch 的 MAC 是 `28:84:85:43:95:60`，两者不能混刷。

## 屏幕

320×240 状态页显示无线连接、按住讲话状态、实时音量条、StopWatch 电池电压、USB 连接和累计音频丢包。电池电压不是经过标定的电量百分比。显示的是远端 StopWatch 麦克风状态，接收器自身的双麦克风、扬声器和摄像头不启用。

屏幕采用 ST7789；SPI3 的 MOSI/SCLK/DC 分别为 GPIO40/41/39，模式2，80MHz。背光 GPIO42 低电平有效，使用 LEDC 25kHz、10bit 分辨率、反相输出，亮度75%；初始化明确设置引脚复用，避免保留默认 MTMS 功能。PCA9557 地址0x19，I2C SDA/SCL 为 GPIO1/2；IO0 是 LCD_CS，IO1 保持关闭功放，IO2 保持摄像头休眠。使用 IDF 新版 I2C API，避免与 M5GFX 混用旧驱动。

实体传输采用 ESP-IDF `esp_lcd` ST7789 驱动，M5GFX 仅负责离屏绘图。初始化沿用原厂顺序：保持 CS 高，建立 SPI/面板 IO，先调用 `esp_lcd_panel_reset` 完成首笔 SPI 交易，使模式2时钟进入稳定状态；再拉低 CS，执行面板初始化、反色、方向与显示设置。CS 高时屏幕不会接收这笔软件复位命令。帧缓存按16行复制到固定内部 DMA 缓冲，每次等待传输完成后复用，避免异步 SPI 读取时覆盖数据。诊断包含 `screen_driver=esp_lcd` 和出错阶段/错误码；`screen=1` 仅表示软件初始化完成，SPI 没有屏幕读回通道。

原厂完整固件恢复后实屏正常；同时匹配80MHz和上述 CS 时序后，用户已确认 RGB 测试画面及状态页正常。尚未单独区分时钟频率与初始化时序各自的贡献，去除临时彩条测试后的最终版本也已由用户完成断电5秒重插验证，状态页正常。

资料：[开发板](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/)、[液晶](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/lcd-display.html)、[IO扩展](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/audio-output-es8311.html)、[I2C](https://wiki.lckfb.com/zh-hans/szpi-esp32s3/beginner/attitude-sensor.html)。

## 无线和 USB

接收器建立仅供这一对设备使用的 WPA2 网络 `StopWatch-C480`，不需要家用路由器，也不连接互联网。随机密码只在本地 `private/pairing.h` 中，源码仓库不包含密码；两端必须用同一份文件构建。

- TCP 传送有序的键盘、滚轮和状态信息，设置 TCP_NODELAY；空闲时约每20ms发送一次状态。
- UDP 每10ms传送480个48kHz/16bit单声道样本；采用30ms起播缓冲，队列上限80ms。连续小缺口填零，旧包/重复包丢弃，大缺口重新缓冲。
- 每次控制连接使用新的随机令牌，每次按住讲话使用新的音频会话；接收器只接收当前会话。松开即清空缓冲，断连释放按键并静音；超时保护350ms。USB音频回调还检查最近音频包不超过80ms。
- 重新连接后须先松开按键，防止旧的 Option/Enter 重放。无线接收器在线时优先走接收器，StopWatch 自身 USB 键鼠/音频输出静音或释放状态；接收器离线后，原有有线方式仍可使用，切换也要求松开按键。
- USB 设备名为 `StopWatch Receiver C480`，包含标准 USB 麦克风、键盘/鼠标及诊断串口。语音输入应用需选择 `StopWatch Wireless Microphone`。键鼠输入不需要电脑安装专用程序。

排查 `SEARCHING` 时，先分别看两条连接：C480 的 `host=1` 表示电脑 USB 已连接，`radio=1` 表示与 StopWatch 的控制会话有效。`ap_stations=1` 只证明有 Wi-Fi 站点关联，不等于控制会话已建立。StopWatch 的 `associated`、`wifi_up`、`wifi_reason`、`wifi_attempts`、`wifi_connect_err`、`wifi_timeouts` 和 `tcp_errno` 用于区分无线关联、静态 IP 与控制连接阶段；断线原因和错误计数可能保留上一次失败，需结合当前状态及计数变化查看。

若 StopWatch 的 `associated=1`，接收器却持续 `ap_stations=0`，且控制连接反复超时，可能是接收器快速重启后留下的旧关联。sender 的 `tcp_fail=次数/毫秒` 记录连续控制失败，`tcp_recover` 记录为恢复通信主动重新关联的次数，`tcp_disc_err` 是最近一次断开请求的返回值。`bssid` 与接收器 `ap_mac` 应一致，`ch` 为当前信道；用实际读数确认热点身份，不能把芯片基础 MAC 直接当成 AP MAC。

无线时开启 Wi-Fi，续航、最大距离和抗干扰能力需实测，不能沿用有线待机的功耗估计。原始音频不压缩，避免编解码损耗；无线延迟和丢包仍可能影响语音。

## 构建和恢复

在项目根目录：

```sh
python3 tools/create_pairing.py
bash tools/build.sh build
bash tools/build_receiver.sh build
```

先核对原固件备份，再分别调用 `tools/flash.py`（StopWatch）和 `tools/flash_receiver.py`（C480）。脚本核对 USB 序列号、完整出厂备份校验以及芯片MAC；串口路径不是身份。

接收器完整16MiB原固件位于仓库之外，相对于项目根目录为 `../backups/receiver-9c139e8ac480/original-20261005.bin`，同名JSON包含SHA256。备份可能含小智配置，不能公开上传。恢复时验证MAC，再将完整备份写入0x0，DIO/16MB/80MHz并执行watchdog reset。

接收器保留原NVS的0x9000/0x4000分区大小，不自动清空已有NVS。新应用替换原小智程序；需要恢复小智时使用完整备份。
