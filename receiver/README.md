# StopWatch 无线接收器

支持 C480 和 Waveshare ESP32-S3-GEEK 两块板。两者复用无线协议和 USB 键鼠/音频实现，屏幕与内存配置独立构建，不能混刷。

| 板子 | 芯片 MAC | USB 产品 / PID | 麦克风名称 |
|---|---|---|---|
| C480 | `9C:13:9E:8A:C4:80` | `StopWatch Receiver C480` / `CAFE:4021` | `StopWatch Wireless Microphone` |
| GEEK-988C | `D4:05:92:78:98:8C` | `StopWatch Receiver GEEK` / `CAFE:4022` | `StopWatch GEEK Microphone` |

## GEEK-988C

16MB Flash、2MB **Quad PSRAM**（40MHz，启用启动内存测试）；独立的 `sdkconfig.geek` 和 `build-geek` 避免沿用 C480 的 Octal 配置。保留原厂 NVS 的 `0x9000/0x5000` 边界。刷机工具要求这台设备的完整16MiB备份及 SHA-256 校验，核对实际 MAC、编译配置、ELF中的USB身份和 ELF/bin 匹配后，仅写 bootloader、分区表和应用，不擦除整片。

```sh
bash tools/build_geek.sh build
python tools/flash_geek.py
python tools/capture_frame.py --geek --output reports/geek.png
```

上述命令从仓库根目录运行，需要已安装的 ESP-IDF 5.5/Python 环境和现有 `private/pairing.h`。本地原厂备份路径为仓库同级 `backups/receiver-d4059278988c/original-20261010.bin`，不上传。USB 序列号为 `D4059278988C-VIBE1`；恢复下载时必须使用该设备对应的两步维护命令。

GEEK 是替换接收器，沿用已配对 StopWatch 的 `StopWatch-C480` 网络名称和私有密码，因此无需给手表重新刷机。**使用 GEEK 时关闭旧 C480，使用 C480 时拔下 GEEK**；两者同时开启会让手表选择其中一个，不提供双接收器广播。电脑不需要加入此 Wi-Fi。电脑输入设备选择 `StopWatch GEEK Microphone`。

1.14英寸240×135 ST7789横屏显示连接/讲话状态、音量条、手表电池电压、USB状态和丢包数。SCLK/MOSI/CS/DC/RST为GPIO12/11/10/8/9；mode0、27MHz，横屏偏移40/53、MADCTL0x70和反色沿用官方示例。GPIO7背光高有效，1kHz PWM、75%亮度；GPIO19/20专用于USB。GPIO0 BOOT运行时仅用于唤醒屏幕，不触发下载；按住BOOT再接USB仍可进入ROM恢复。GEEK没有触摸屏，也没有板载麦克风；声音来自无线连接的StopWatch。

30分钟没有有效交互后关闭背光，USB/Wi-Fi继续运行；收到手表按键、滚轮、PTT或短按本机BOOT唤醒。屏幕初始化与DMA传输失败不会阻止USB和无线启动。诊断含`board=geek`、`psram_size`、屏幕错误及尺寸，截图响应为`FRAME 240 135 64800`；软件帧缓存不能替代实屏验收。

参考：[Waveshare GEEK](https://docs.waveshare.com/ESP32-S3-GEEK)、[官方示例](https://files.waveshare.com/wiki/ESP32-S3-GEEK/ESP32-S3-GEEK-Demo.zip)（`MPY/LCD/lcd_example.py`）、[原理图](https://files.waveshare.com/wiki/ESP32-S3-GEEK/ESP32-S3-GEEK-Schematic1.pdf)。

## C480

硬件：立创实战派 ESP32-S3，N16R8，出厂 MAC `9C:13:9E:8A:C4:80`。
配套 StopWatch 的 MAC 是 `28:84:85:43:95:60`，两者不能混刷。

### C480 屏幕

320×240 状态页显示无线连接、按住讲话状态、实时音量条、StopWatch 电池电压、USB 连接和累计音频丢包。电池电压不是经过标定的电量百分比。显示的是远端 StopWatch 麦克风状态，接收器自身的双麦克风、扬声器和摄像头不启用。

连续30分钟没有本机交互或有效控制输入后，C480 关闭背光并停止画面刷新；USB、Wi-Fi、键鼠和音频处理保持运行。触摸 C480 屏幕、运行时短按 BOOT，或收到 StopWatch 的有效按键、滚轮、按住讲话（PTT）输入即可唤醒。普通连接心跳、电量/电平更新、USB轮询和空闲静音音频不会延长亮屏；持续按住讲话仍属于交互，即使没有说话。StopWatch 的屏幕独立计时，由其黄/蓝键或触摸本机屏幕唤醒。

唤醒时先刷新状态页，再恢复背光，不重新初始化屏幕或切断供电。`display_awake`、`display_idle_ms` 和 `display_timeout_ms` 分别报告亮屏状态、距最近交互的毫秒数和息屏间隔，生产默认间隔为 `1800000`。本功能未实测电流或续航增益，也不改变此前非预期黑屏故障的验收限制。

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

## C480 USB 连接与 Hub 层级

使用带数据传输功能的 Type-C 线和正常5V供电即可；当前 ESP32-S3 接口以 USB 2.0 全速12Mbps运行，不要求USB 3.0或专用线。固件声明最大500mA供电预算，这不是实测电流。

板上另有 CH334F USB Hub，Type-C 上行先经过它，两个下行分别连接 ESP32-S3 原生USB和独立 CH340K 串口。因此接入外部扩展坞时，会比直接连接的鼠标等设备多一级 Hub；一个实体扩展坞内部也可能有多级芯片。

2026-10-06 本机用同一根线对照：正常路径含外部1级和板载1级 Hub，两路下行设备均可识别；另一条路径含外部4级和板载1级，macOS 明确记录 `AppleUSBHostPort::setPortLocation: hub depth exceeded`，板载 Hub 自身可见但下行端口没有建立，接收器显示「等待 USB」。这是该 Mac/连接路径的实际层级限制，不能仅凭其他设备可用就排除。

处理方式是减少串接层级：把 C480 接电脑本机接口，或接直接连电脑的 Hub；多口扩展坞上不同端口的内部层级也可能不同。继续在原路径末端加 Hub 不会解决此问题。此类「等待 USB」与 StopWatch 无线 SEARCHING 分别排查，不需要重刷固件。

## 构建和恢复

在项目根目录：

```sh
python3 tools/create_pairing.py
bash tools/build.sh build
bash tools/build_receiver.sh build
```

息屏计时及有效控制输入判断可在项目根目录运行原生测试：

```sh
clang++ -std=c++17 -Wall -Wextra -Werror tests/display_idle.cpp -o /tmp/stopwatch-display-idle-test
/tmp/stopwatch-display-idle-test
```

先核对原固件备份，再分别调用 `tools/flash.py`（StopWatch）和 `tools/flash_receiver.py`（C480）。脚本核对 USB 序列号、完整出厂备份校验以及芯片MAC；串口路径不是身份。

接收器完整16MiB原固件位于仓库之外，相对于项目根目录为 `../backups/receiver-9c139e8ac480/original-20261005.bin`，同名JSON包含SHA256。备份可能含小智配置，不能公开上传。恢复时验证MAC，再将完整备份写入0x0，DIO/16MB/80MHz并执行watchdog reset。

接收器保留原NVS的0x9000/0x4000分区大小，不自动清空已有NVS。新应用替换原小智程序；需要恢复小智时使用完整备份。
