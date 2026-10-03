# StopWatch Vibe

M5Stack StopWatch C152 的有线 USB 麦克风和双键键盘固件，面向 Mac 上的语音编程。当前已刷入版本为 v4。

- 黄色左键：右 Option（USB Right Alt，按住保持、松开释放）。
- 蓝色键：Enter。两键同时按是 Option+Enter；长按 Enter 的重复行为由电脑决定。
- 麦克风：内置 ES8311，48 kHz / 16 bit / 单声道 USB Audio；始终采集，无须按键，不启用 Wi-Fi 或蓝牙。
- 屏幕：黑底青绿色麦克风，音量驱动闪动；黄色键按住时变为琥珀色脉动。红色代表麦克风初始化失败。
- 断线重插时，必须先松开两键，避免重放旧按键。

macOS 将它识别为 `StopWatch Vibe`，使用系统自带的 USB 音频和键盘驱动。首次使用请在系统或目标应用中选择这个输入设备；USB 固件无法强制另一台电脑切换默认麦克风。语音转文字和快捷键触发行为由电脑上的应用负责，固件不做识别、不保存录音、不发送网络请求。

## 构建与刷写

使用 ESP-IDF 5.5.0、Xtensa 14.2.0、TinyUSB 0.18.0~2。M5Unified 和 M5GFX 以固定提交的 Git 子模块保存；TinyUSB 为供应商组件源码，保留原始许可证。详见 [依赖记录](THIRD_PARTY.md)。

在新电脑上，先安装 ESP-IDF 5.5.0 并激活其环境，再执行：

```sh
git clone --recurse-submodules https://github.com/alfred-bot-001/m5-stopwatch.git
cd m5-stopwatch
source /path/to/esp-idf/export.sh
bash tools/build.sh build
```

本机已有工具链的工作区也可直接运行下列命令：

```sh
bash tools/build.sh build
../.tools/esp32-test/bin/python tools/flash.py
clang++ -std=c++17 tests/buttons.cpp -o /tmp/stopwatch-buttons-test
/tmp/stopwatch-buttons-test
```

`tools/flash.py` 是这台已验证设备的保护性刷写入口，依赖本地 `backups/` 文件，GitHub 不包含这些备份。它校验完整原固件备份及设备身份，只刷写指定 StopWatch；使用 watchdog reset 退出下载模式。不要在启动过程中用通用串口程序操作 `303A:1001` 的 DTR/RTS，否则可能重新进入下载模式。固件诊断接口为 `CAFE:4020`，串口名称可随 USB 插口变化。

诊断串口每秒输出 `VIBE` 状态：采样计数、电平、按键、运行时间、重启原因及错误计数。`P` 返回 `FRAME 360 360 259200` 后接 RGB565 大端屏幕帧；`B` 返回 ROM 下载模式。`drops` 包含电脑未打开麦克风时主动丢弃的旧音频，不等同于录音丢帧；电脑录音期间应检查 `underflows` 和 `errors` 不增长。

其他 StopWatch 请先使用 esptool 完整备份自己的 16 MiB Flash，再按构建输出中的地址刷写 bootloader、partition table 和 app，并使用 `--after watchdog-reset`。当前 USB 序列号是这台个人原型的固定编号，多台同时使用时应改为各自唯一编号。

## 原固件恢复

`backups/original-288485439560-20261003.bin` 是完整 16 MiB 备份，SHA-256 和设备信息在同名 JSON 中。恢复前核对设备 MAC，进入 ROM 下载模式后将备份写入地址 `0x0`，刷写参数为 16MB / DIO / 80MHz，并使用 `--after watchdog-reset`。备份可能包含原设备配置，请勿公开上传。

## 验证记录

本地构建、烧录及实际运行日志在未上传的 `reports/`。可公开的验收范围见 [验证记录](VALIDATION.md)。按键逻辑测试覆盖防抖、长按保持、组合键、松开、重连抑制和计时溢出。USB 麦克风已通过 CoreAudio 实际采集；屏幕截图为设备帧缓存读回，不能替代用户对物理显示和实体按键的确认。

开发 VID/PID 为 `CAFE:4020`，用于这台个人原型，不作为商业产品的正式 USB 标识。
