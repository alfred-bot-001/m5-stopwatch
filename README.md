# StopWatch Vibe

M5Stack StopWatch C152 的 USB 麦克风、双键键盘和触摸滚轮固件，面向 Mac 上的语音编程。v11 增加[立创实战派 ESP32-S3 无线接收器](receiver/README.md)，已完成无线讲话试测及接收器状态页、断电重插验证，最终无线键鼠验收仍待完成；此前已验证的有线版本为 v10。StopWatch 闲置黑屏的根因仍在调查。实测范围见 [验证记录](VALIDATION.md)。

- 黄色左键：按住开启麦克风，同时保持右 Option（USB Right Alt）；松开关闭采集并释放按键。
- 蓝色键：Enter。两键同时按是 Option+Enter；长按 Enter 的重复行为由电脑决定。
- 麦克风：内置 ES8311，48 kHz / 16 bit / 单声道 USB Audio。未按黄色键时停止 I2S 采集，关闭 ADC 模拟电路、清空缓存，USB 音频接口保持连接并输出静音。v11 启用 Wi-Fi 连接配套接收器，不启用蓝牙。芯片重新启动需要时间，特别是首次冷启动可能约一秒，按住后稍等再说话。
- 屏幕左右滑：发送左/右方向键，移动文字光标；每滑动约 24 屏幕像素一步。若同时按黄色键，电脑会收到 Option+方向键。
- 屏幕上下滑：发送鼠标滚轮，上滑为正向滚轮、下滑为反向；实际页面方向受 macOS 的「自然滚动」设置影响。手势锁定首次明确的横/纵方向，抬手后重新判定；轻点和小幅抖动不触发。
- 屏幕：闲置时显示青绿色带斜线麦克风，按住黄色键时显示琥珀色脉动麦克风。红色代表麦克风初始化失败。待机绘制频率降为约 10fps；未测量整机电流或续航增益。
- 断线重插时，必须先松开两键，避免重放旧按键。
- 启动时关闭 M5PM1 状态灯输出，保留充电和供电位。v7 另提供受保护的整机电源重启命令；本机执行后，用户确认此前持续闪烁的绿灯已停止，详情见验证记录。

macOS 将有线 StopWatch 识别为 `StopWatch Vibe`，将接收器识别为 `StopWatch Receiver C480`，使用系统自带的 USB 音频、键盘和鼠标驱动。音频输入列表中的名称分别是 `StopWatch Vibe Microphone` 和 `StopWatch Wireless Microphone`。键盘与鼠标共用 HID 接口，以 Report ID 1/2 区分，适配 ESP32-S3 的输入端点数量限制。首次使用请在系统或目标应用中选择对应输入设备；USB 固件无法强制另一台电脑切换默认麦克风。语音转文字和快捷键触发行为由电脑上的应用负责，固件不做识别、不保存录音。v11 只在两端的专用无线网络传送音频和控制信息，不连接互联网。

## 构建与刷写

使用 ESP-IDF 5.5.0、Xtensa 14.2.0、TinyUSB 0.18.0~2。M5Unified 和 M5GFX 以固定提交的 Git 子模块保存；TinyUSB 为供应商组件源码，保留原始许可证。详见 [依赖记录](THIRD_PARTY.md)。

在新电脑上，先安装 ESP-IDF 5.5.0 并激活其环境，再执行：

```sh
git clone --recurse-submodules https://github.com/alfred-bot-001/m5-stopwatch.git
cd m5-stopwatch
source /path/to/esp-idf/export.sh
python3 tools/create_pairing.py
bash tools/build.sh build
```

本机已有工具链的工作区也可直接运行下列命令：

```sh
bash tools/build.sh build
../.tools/esp32-test/bin/python tools/flash.py
clang++ -std=c++17 tests/buttons.cpp -o /tmp/stopwatch-buttons-test
/tmp/stopwatch-buttons-test
clang++ -std=c++17 tests/gestures.cpp -o /tmp/stopwatch-gestures-test
/tmp/stopwatch-gestures-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/diagnostics.cpp -o /tmp/stopwatch-diagnostics-test
/tmp/stopwatch-diagnostics-test
python3 -B tests/diagnose.py
# 在 ESP-IDF Python 环境中（需要 pyelftools）：
python tests/usb_descriptors.py build/stopwatch_vibe.elf
```

`tools/flash.py` 是这台已验证设备的保护性刷写入口，依赖本地 `backups/` 文件，GitHub 不包含这些备份。它校验完整原固件备份及设备身份，只刷写指定 StopWatch；使用 watchdog reset 退出下载模式。不要在启动过程中用通用串口程序操作 `303A:1001` 的 DTR/RTS，否则可能重新进入下载模式。固件诊断接口为 `CAFE:4020`，串口名称可随 USB 插口变化。

v11 先显示首帧、启动 USB，再在后台启动无线。开机同时按住蓝、黄两键可跳过无线，显示 `USB SAFE / WIFI OFF`；完全松开两键后才响应键盘输入。无线初始化失败会保留有线功能，并在屏幕及诊断字段 `radio_err` / `radio_stage` / `radio_errno` 中显示原因。该入口无法防止所有驱动内部崩溃，不能替代硬件下载恢复。

两份刷写脚本都会先将本次 ELF、固件、分区表和 bootloader 按 ELF SHA-256 归档到本地 `reports/firmware-archive/`。分析崩溃记录必须匹配当次 ELF；重编译后的符号即使来自相近源码，也不能当作原固件的精确定位。这些文件可能包含配对密码，不公开上传。

诊断串口每秒输出 `VIBE` 状态：采样计数、电平、按键、运行时间、重启原因及错误计数。v5 还包含 BOOT 引脚 `g0`、黄/蓝按键累计次数 `presses`、电源按键状态 `pm_btn` 和配置 `pm_cfg`，用于调查按黄色键后黑屏。v6 增加电源寄存器 `pm_pwr` 和 LED 配置读回校验 `led_cfg_ok`；v7 增加 PMIC 标识 `pm_id`、第二按键配置 `pm_cfg2`、GPIO 复用/驱动/输入 `pm_func`/`pm_drv`/`pm_io`，以及本轮读取是否成功的 `pm_valid`。`P` 返回 `FRAME 360 360 259200` 后接 RGB565 大端屏幕帧。

维护命令必须成对发送完整行，两步间隔不超过 2 秒。进入 ROM 下载模式使用 `VIBE/1 ARM-BOOT 288485439560\n` 与 `VIBE/1 CONFIRM-BOOT 288485439560\n`。通过 M5PM1 重启整机使用 `VIBE/1 ARM-RESET 288485439560\n` 与 `VIBE/1 CONFIRM-RESET 288485439560\n`；仅在显式收到这组命令时执行一次，不会每次启动自动重启。两种确认不能混用，单字节 `B` 不再触发复位。后者会短暂断开 USB，适用于排查仅重启 ESP32 后仍保留的 PMIC 状态。

v8 中 `mic` 表示硬件采集是否运行，`listen` 表示当前是否请求按住采音；闲置时两者应为 0，`samples` 应停止增长。`gestures` 是累计发送到队列的水平光标步数/滚轮步数，`gesture_drops` 是队列满时丢弃的事件数，不能将其当作电脑已经处理手势的证明。`drops` 包含按住黄色键但电脑未打开麦克风时主动丢弃的旧音频，不等同于录音丢帧；静音期间不增加 `underflows`。

其他 StopWatch 请先使用 esptool 完整备份自己的 16 MiB Flash，再按构建输出中的地址刷写 bootloader、partition table 和 app，并使用 `--after watchdog-reset`。当前 USB 序列号是这台个人原型的固定编号，多台同时使用时应改为各自唯一编号。

## 闲置故障记录

v9 的 `pm_sleep`、`pm_wdt`、`pm_timer` 分别记录 PMIC 休眠配置、看门狗倒计时、定时器配置/计数；`pm_src`/`pm_wake` 是电源和唤醒标志，`mv` 是电池/USB 电压（mV）。这些读数只说明采样当时状态，初始化也会清除部分 PMIC 设置，不能反推未记录的故障瞬间。

需要排查闲置故障时，可用 `python tools/diagnose.py --seconds 86400 --output reports/idle-watch.jsonl` 记录最多 24 小时状态（需 pyserial）。该工具仅匹配本机原型，常态每 5 秒保存一行，并在关键状态变化时立即保存；不录音、不发送维护命令，不打开 ROM 下载串口，也不会自动恢复设备，以保留故障现场。v10 工具每 30 秒记录主机心跳，同时记录进程 PID、退出原因及休眠恢复的时间间隔。可传 `--deadline-unix`（Unix 秒）让监督进程重启后仍遵守同一个截止时间，避免不断延长 24 小时期限。Mac 睡眠/关机或进程结束时无法继续记录；正常使用固件不依赖此工具。刷机前应停止日志进程，避免两个串口读取者竞争诊断输出。

v10 的 `pm_events` 累计保存读到的 PMIC 按键事件，`pm_evt_ms` 为最近事件的运行时间，`pm_hold`/`pm_hold_max` 为观察到的 bit0 当前/最长高电平毫秒数（按手册解释为按下，尚需核对本机实际按下/释放）。官方手册规定 `pm_btn` bit0 为 1 表示按下，bit7 为读取后清除的事件；一次读取间隔内的多次事件可能合并。累计记录避免短暂事件在每秒输出前被下一次读取覆盖，但不是独立的物理按键测量。

`boot_raw`/`boot_strap` 在应用入口保存原始复位/启动引脚值；`usb_ev` 从低到高四个字节为 USB mount、unmount、suspend、resume 次数，各自按 256 回绕。`usj_clk=0` 表示闲置 USB Serial/JTAG 时钟关闭；正常 USB 音频、键盘和 CDC 使用独立的 OTG 控制器。受保护维护下载前会重新开启 USJ。关闭它是缩小排查范围，尚未证明能解决本次故障。

`intent` 为 0（正常）、1（显式维护下载）或 2（显式 PMIC 重启）。RTC 内存以版本和校验保护最后一份状态；`prev` 依次输出有效标志、运行毫秒、维护意图、PMIC 打包状态、标志、按键事件数、最近事件时间、当前/最长按住时间、原始复位/启动引脚值、USB 事件计数。PMIC 打包状态低到高字节是 BTN/BTN_CFG1/PWR_CFG/WAKE_SRC；标志 bit0/1/2 是 PMIC 本次读取有效/USB 已连接/黄色键按住。RTC 仅在部分 ESP 复位中保持，掉电或完整硬件重置可能丢失；`prev=0,...` 不证明重启前没有异常。若已进入 ROM，应先保留现场，再依据该次 ELF 的 `retained_diagnostic` 符号读取；打开原生 ROM 串口本身可能改变复位原因，不能把读到的最后复位原因直接当作原始故障原因。

本次排查可使用临时 launchd 任务监督日志进程，固定截止时间后正常退出；不写入开机自启目录，也不阻止 Mac 睡眠。刷机前先停止该任务：`launchctl bootout gui/$(id -u)/local.stopwatch.vibe.diagnostics`。本地原始诊断文件位于忽略的 `reports/`，不上传。

## 原固件恢复

`backups/original-288485439560-20261003.bin` 是完整 16 MiB 备份，SHA-256 和设备信息在同名 JSON 中。恢复前核对设备 MAC，进入 ROM 下载模式后将备份写入地址 `0x0`，刷写参数为 16MB / DIO / 80MHz，并使用 `--after watchdog-reset`。备份可能包含原设备配置，请勿公开上传。

2026-10-05 本机在普通长按红键、重新插线均无串口回应后，通过外部扩展口将 **G0/BOOT 与 GND** 临时连接，再长按红键约 2 秒，成功恢复 ROM 通信；随后移除跳线并保持 USB 连接。仅按官方引脚标记辨认 G0 和 GND，不连接 BAT、5V 或 3V3。早期 v1.0 背贴的无星号 `BAT` 实际是 `5V IN`，见 [StopWatch 官方引脚及修订说明](https://docs.m5stack.com/en/core/StopWatch)。固件损坏不意味着 ROM 被擦除；本次已从完整备份恢复原厂固件并通过烧录校验。

## 验证记录

本地构建、烧录及实际运行日志在未上传的 `reports/`。可公开的验收范围见 [验证记录](VALIDATION.md)。按键逻辑测试覆盖防抖、长按保持、组合键、松开、重连抑制和计时溢出。USB 麦克风已通过 CoreAudio 实际采集；屏幕截图为设备帧缓存读回，不能替代用户对物理显示和实体按键的确认。

开发 VID/PID 为 `CAFE:4020`，用于这台个人原型，不作为商业产品的正式 USB 标识。
