# 第三方依赖

| 依赖 | 固定版本 / 提交 | 来源与许可 |
| --- | --- | --- |
| M5Unified | `fd40d58b8405ad1e7ed7afab548dab5e7cec5bbb` | https://github.com/m5stack/M5Unified ，许可见子模块 LICENSE |
| M5GFX | `c5a3fefad0b38a52cc750e2c4761e339a69a7208` | https://github.com/m5stack/M5GFX ，许可见子模块 LICENSE |
| Espressif TinyUSB | `0.18.0~2` / `4095aba50e97e84b944693b38ab58b54e14bd311` | https://github.com/espressif/tinyusb ，MIT；源码及许可证位于 `components/tinyusb/` |

ESP-IDF 5.5.0 和 Xtensa 工具链需要单独安装，不随此仓库提供。M5Unified/M5GFX 必须使用递归克隆或执行 `git submodule update --init --recursive`。

USB 描述符模板和 UAC 控制布局基于 TinyUSB。应用 USB 实现由同一工作区的 ESP32 Siri Voice Pad 原型适配，按键映射及硬件初始化针对 StopWatch。
