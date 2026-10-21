# StopWatch Vibe

M5Stack StopWatch C152 的 USB 麦克风、双键键盘和触摸滚轮固件，面向 Mac 上的语音编程。v11 增加[立创 C480 / Waveshare GEEK 无线接收器](receiver/README.md)，已完成无线讲话试测及接收器状态页、断电重插验证，最终无线键鼠验收仍待完成；此前已验证的有线版本为 v10。此前 StopWatch 非预期闲置黑屏的根因仍在调查。实测范围见 [验证记录](VALIDATION.md)。

- 黄色左键：按住开启麦克风，同时保持右 Option（USB Right Alt）；松开关闭采集并释放按键。
- 蓝色键：Enter。两键同时按是 Option+Enter；长按 Enter 的重复行为由电脑决定。
- 红色电源键：默认关闭单击复位，长按下载门槛由原厂2秒延长到4秒，保留双击关机。可显式构建禁止红键下载的版本，见下文；该模式会锁定 M5PM1 的下载入口，解除需要电源芯片完全断电。默认配置不主动上锁，也不会尝试解除已经存在的锁。诊断 `key_guard=1` 表示配置写入并读回成功；默认 `pm_cfg=3b`，启用下载锁后为 `bb`。
- 麦克风：内置 ES8311，48 kHz / 16 bit / 单声道 USB Audio。未按黄色键时停止 I2S 采集，关闭 ADC 模拟电路、清空缓存，USB 音频接口保持连接并输出静音。v11 启用 Wi-Fi 连接配套接收器，不启用蓝牙。芯片重新启动需要时间，特别是首次冷启动可能约一秒，按住后稍等再说话。
- 屏幕左右滑：发送左/右方向键，移动文字光标；每滑动约 24 屏幕像素一步。若同时按黄色键，电脑会收到 Option+方向键。
- 屏幕上下滑：发送鼠标滚轮，上滑为正向滚轮、下滑为反向；实际页面方向受 macOS 的「自然滚动」设置影响。手势锁定首次明确的横/纵方向，抬手后重新判定；轻点和小幅抖动不触发。
- 屏幕：橙色小宠物在整块466×466圆屏内随倾斜、晃动滑动，沿屏幕圆周反弹，撞击时短暂压扁；屏幕倾斜时按重力辨认现实中的上方，侧转或倒转手表后，小宠物带着角惯性沿较短方向转正：先逐渐加速、停手后顺势转动，再轻轻回弹收住。下落和转身会有小幅肉团拉伸、侧弯及弹性回弹；蹭到圆屏边缘时，接触摩擦会拖慢滑动和转正，离开边缘后恢复。重量、惯性与弹性是视觉物理模型的归一化参数，不代表实际克数。接近平放、重力投影不足时保持最后稳定朝向，强冲击不直接作为新的朝向；连续碰撞后出现旋涡眼和绕头金色五角星，静放后逐渐恢复。较强且连续的晃动会生气，随后短暂委屈、害羞，再恢复开心；固定倾斜、孤立异常样本和小幅噪声不会触发生气。角色缓存放大到240×240，比原180×180宽高增加约33%，不再绘制内嵌围栏。表情用约360ms渐变切换，过程中再次切换会从当前画面继续过渡。采用用户提供的八套小橙蛙表情：开心、大笑、惊讶、害羞、难过、生气、犯困和眨眼。按住黄色键并检测到声音时，普通瞳孔轻轻转动，靠下的嘴巴随音量开合；声音较强时使用大笑表情，安静或松开后闭嘴回正。轻碰会短暂惊讶，安静时偶尔眨眼，静放2分钟显示静态犯困表情。表情是按交互和音量触发，不做语义或情绪识别。连接文字与状态点在宠物靠近下缘时移到顶部避让；按住黄色键时状态点呈琥珀色，红色表示麦克风初始化失败。连续30分钟无交互后自动息屏，按黄/蓝键、触摸屏幕或明显晃动本机可唤醒。移动或讲话时缩短刷新间隔，静止且画面状态不变时停止重绘，自动表情不会延长息屏计时，息屏停止绘制；未测量整机电流或续航增益。
- 断线重插时，必须先松开两键，避免重放旧按键。
- 启动时关闭 M5PM1 状态灯输出，保留充电和供电位。v7 另提供受保护的整机电源重启命令；本机执行后，用户确认此前持续闪烁的绿灯已停止，详情见验证记录。

macOS 将有线 StopWatch 识别为 `StopWatch Vibe`，将接收器识别为 `StopWatch Receiver C480`，使用系统自带的 USB 音频、键盘和鼠标驱动。音频输入列表中的名称分别是 `StopWatch Vibe Microphone` 和 `StopWatch Wireless Microphone`。键盘与鼠标共用 HID 接口，以 Report ID 1/2 区分，适配 ESP32-S3 的输入端点数量限制。首次使用请在系统或目标应用中选择对应输入设备；USB 固件无法强制另一台电脑切换默认麦克风。语音转文字和快捷键触发行为由电脑上的应用负责，固件不做识别、不保存录音。v11 只在两端的专用无线网络传送音频和控制信息，不连接互联网。

## 有线与无线使用

GEEK-988C 接收器的 USB 名称是 `StopWatch Receiver GEEK`，麦克风选择 `StopWatch GEEK Microphone`。它沿用手表原配对信息；GEEK 与旧 C480 只开启其中一台，避免手表连接到另一个接收器。GEEK 的小屏是240×135，无触摸，运行时短按BOOT或操作手表可唤醒，保留30分钟息屏。构建与防混刷步骤见[接收器说明](receiver/README.md)。

StopWatch 圆屏边缘有一圈约4像素宽的电量环：从12点顺时针的绿色弧代表剩余电量，其余弧为红色；100%全绿、0%全红。不另加内框，也不缩小小宠物。百分比按电压3300–4200mV估算，采用约8秒平滑和1%迟滞抑制波动，充电和负载仍会影响它，不能当作精确剩余容量。启动尚无有效读数或连续5秒读数无效时显示灰环；电量更新不会唤醒屏幕或延长30分钟息屏计时。圆环随电量变化完整刷新，宠物动画继续使用局部刷新。

- 有线：StopWatch 用数据线连接电脑，直接作为 USB 键鼠和麦克风使用。
- 无线：C480 用数据线连接电脑，StopWatch 自动连接 C480 建立的 `StopWatch-C480` 专用 Wi-Fi，再由 C480 输出 USB 键鼠和麦克风。StopWatch 可以用电池供电；电脑无需加入该 Wi-Fi，也无需家用路由器。
- 两条通路自动选择。StopWatch 显示 `WIRELESS` 时优先走接收器；显示 `USB / SEARCHING` 时仍在寻找接收器，此时自身 USB 有线功能可用。切换后先松开两键，再开始操作。
- C480 的「USB连接」与「StopWatch连接」是两个独立状态。亮屏只说明有供电；「等待 USB」说明尚未完成主机 USB 连接，「等待 StopWatch」说明尚无有效无线控制连接。

两端独立计算30分钟息屏时间。StopWatch 的黄/蓝键、触摸、按住讲话及明显物理移动会延长亮屏；C480 的本机触摸、运行时短按 BOOT，或收到有效按键、滚轮、按住讲话（PTT）控制输入，会保持亮屏或唤醒。普通连接心跳、电量/电平更新、USB轮询、空闲静音、固定倾斜、传感器小幅噪声和宠物自身的动画余波不会延长亮屏；按住讲话本身仍属于交互，即使此时没有说话。息屏仅将 StopWatch AMOLED 亮度设为0或关闭 C480 背光，并停止画面刷新，USB、Wi-Fi 和输入处理保持运行，不关闭 MCU 或 PMIC 电源。

诊断中的 `display_awake` 表示显示是否点亮，`display_idle_ms` 是距最近交互的毫秒数，`display_timeout_ms` 的生产默认值为 `1800000`。这项主动息屏功能不代表此前非预期黑屏、绿灯频闪故障已解决，历史排查限制仍见下文及验证记录。

无线重连在工作任务中执行：连接请求立即失败时按250ms至4s退避重试；连接进行中等待最多15秒再核对实际关联和静态IP，避免打断健康链路。诊断包含关联、断线原因、重试次数及 TCP 错误；接收器报告已关联站点数。配对文件保持不变，不通过擦除 NVS 或反复重启整台设备恢复连接。

C480 快速断电重插时，电池供电的 StopWatch 可能保留旧 Wi-Fi 关联，而接收器已经丢失该连接。固件还检查实际控制应答：即使 Wi-Fi 显示已关联，持续15秒且至少5次连接/协议失败后，也会重新关联热点；此类恢复至少间隔30秒。有效控制应答会清空连续失败窗口，正常待机和讲话不会触发该恢复。

## 电量历史与闲置省电

StopWatch 在独立的1MiB循环Flash分区中，每分钟保存电池/USB电压、估算电量、充电状态、屏幕/PTT/无线省电状态、采样时CPU频率和PMIC充电引脚配置，并额外记录开机、初始状态、外部电源接入/拔出及充电开始/停止。启动立即保存第一条分钟样本，状态变化需约1秒连续有效读数确认；读取失败记录为未知，不凭失败推断充电结束。16384条容量约等于11天分钟记录，事件会缩短保留时间。旧记录按扇区循环覆盖，CRC校验跳过掉电造成的半条写入；已经提交的记录跨重启保留。讲话期间暂缓写入、松键后补写，有限队列溢出或Flash失败会在诊断中报告，不宣称掉电前最后一分钟或无限长录音期间的数据绝不丢失。

日志时间使用`session + uptime_ms`，不假定设备已有准确日期；导出工具另存电脑的导出时间。电量百分比仍为电压估算，没有电流传感器读数，不能据此计算精确mAh。`charge_stop`表示充电状态信号结束，不能单凭它区分充满、拔线或充电器异常；需结合`external_power_off`和电压。`cpu_mhz`是采样任务执行时的频率，通常为240MHz，不能当成一分钟内的平均频率。

插回这台StopWatch的USB后，可使用配置好pyserial的Python导出CSV和同名JSON元数据：

```sh
python tools/power_history.py --output reports/power-history.csv
```

工具只识别`CAFE:4020 / 288485439560-VIBE1`，不复位设备、不清日志。USB `H`命令分段导出固定快照，导出时继续服务按键并暂停日志落盘；断线或导出超时会释放快照。正常刷写只写bootloader/分区表/app，不擦除`powlog`；整片擦除或恢复完整原厂备份会覆盖历史。当前升级前没有采集的数据无法补回。

闲置5秒后，手表Wi-Fi切到`WIFI_PS_MIN_MODEM`，控制心跳20ms→80ms，保持与旧GEEK/C480接收器150ms超时兼容；按键、触摸或明显移动恢复活动状态，首次讲话的有界音频队列等待无线唤醒。处理器启用80–240MHz动态调频，关闭Light-sleep以保留USB；RTOS运行任务时仍会升频，闲置间隙可降至80MHz。静止画面只有电量/连接/按键等可见状态改变才重绘，运动和眼球回正保留动画。30分钟息屏不变。`pm_ok/cpu_floor/pm_boost/frames`、`wifi_ps/control_ms/ps_err`、`hist/hist_drop/hist_err`可核对实际状态；这些配置不能替代整机电流和满充续航测量。充电电流档位未修改。

## 构建与刷写

### 可选红键下载锁

`VIBE_RED_DOWNLOAD_LOCK` 默认 `OFF`。明确接受失去红键硬件下载入口后，可运行 `bash tools/build.sh -DVIBE_RED_DOWNLOAD_LOCK=ON build` 构建候选固件。该选项会保存在当前构建目录的 CMake 缓存中；之后恢复普通构建必须显式传入 `-DVIBE_RED_DOWNLOAD_LOCK=OFF`。构建本身不改变设备，刷入并启动才会写入锁定位。

启用时先完成可逆的单击复位屏蔽、首帧和 USB 初始化，再设置 M5PM1 `BTN_CFG_1(0x49)` 的 `DL_LOCK[7]`，并读回验证；保留双击关机及原单/双击时间，完全不改 `BTN_CFG_2`。没有为红键增加其他应用动作：本机原始按键状态持续01，尚不能可靠用于息屏或快捷键。`key_lock_req` 表示本固件是否请求上锁，`key_lock` 表示实际读回的锁定位；`key_guard` 失败时会增加应用错误。

[官方驱动](https://github.com/m5stack/M5PM1/blob/main/src/M5PM1.cpp)明确指出锁置1后不能通过软件清零，只有 M5PM1 本身完全断电才能解除。普通 ESP 重启、PMIC A2 系统重启、双击关机或仅拔 USB 不等于移除内置电池对 PMIC 的供电；刷回 OFF 固件也不会清掉已存在的锁。锁生效后的常规升级使用已有两步保护 USB 维护命令，它直接设置 ESP32 的下载启动位，不发送 PMIC 下载命令。若应用无法运行，恢复可能需要背面 G0/GND 辅助启动或断开内置电池；锁定状态下的实体跳线恢复尚未验收，不能承诺与原长按恢复一样方便。此选项不能代替对历史异常下载根因的调查。

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
clang++ -std=c++17 -Wall -Wextra -Werror tests/station_reconnect.cpp -o /tmp/stopwatch-reconnect-test
/tmp/stopwatch-reconnect-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/display_idle.cpp -o /tmp/stopwatch-display-idle-test
/tmp/stopwatch-display-idle-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/pet_motion.cpp -o /tmp/stopwatch-pet-test
/tmp/stopwatch-pet-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/pet_orientation.cpp -o /tmp/stopwatch-orientation-test
/tmp/stopwatch-orientation-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/pet_expression.cpp -o /tmp/stopwatch-expression-test
/tmp/stopwatch-expression-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/pet_transition.cpp -o /tmp/stopwatch-transition-test
/tmp/stopwatch-transition-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/pet_deformation.cpp -o /tmp/stopwatch-deformation-test
/tmp/stopwatch-deformation-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/render_idle.cpp -o /tmp/stopwatch-render-idle-test
/tmp/stopwatch-render-idle-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/power_key.cpp -o /tmp/stopwatch-power-key-test
/tmp/stopwatch-power-key-test
clang++ -std=c++17 -Wall -Wextra -Werror tests/pmic_diagnostics.cpp -o /tmp/stopwatch-pmic-test
/tmp/stopwatch-pmic-test
python3 -B tests/diagnose.py
# 在 ESP-IDF Python 环境中（需要 pyelftools）：
python tests/usb_descriptors.py build/stopwatch_vibe.elf
```

`tools/flash.py` 是这台已验证设备的保护性刷写入口，依赖本地 `backups/` 文件，GitHub 不包含这些备份。它校验完整原固件备份及设备身份，只刷写指定 StopWatch；使用 watchdog reset 退出下载模式。不要在启动过程中用通用串口程序操作 `303A:1001` 的 DTR/RTS，否则可能重新进入下载模式。固件诊断接口为 `CAFE:4020`，串口名称可随 USB 插口变化。

v11 先显示首帧、启动 USB，再在后台启动无线。开机同时按住蓝、黄两键可跳过无线，显示 `USB SAFE / WIFI OFF`；完全松开两键后才响应键盘输入。无线初始化失败会保留有线功能，并在屏幕及诊断字段 `radio_err` / `radio_stage` / `radio_errno` 中显示原因。该入口无法防止所有驱动内部崩溃，不能替代硬件下载恢复。

宠物使用内置 BMI270，初始化放在首帧及 USB 启动之后；亮屏读取间隔至少25ms，息屏至少200ms，实际频率受主线程绘图耗时影响。只处理驱动报告的新加速度，超过300ms无有效样本即停止施加旧姿态的力。低频读取没有关闭 IMU 的硬件供电。诊断 `imu` 是初始化结果，`imu_samples`/`imu_fail` 是有效样本/未取得有效新样本的次数，`accel` 是映射到屏幕的三轴毫 g，`pet` 是角色中心坐标，`hits` 是有效碰撞累计次数，`dizzy`与`angry`为0–100；`face`依次0–9表示开心、说话、大笑、惊讶、害羞、难过、生气、犯困、眨眼、晕眩，`mouth`为0–100的讲话开口强度，`face_fade`为0–100的表情渐变进度（100表示完成），`angle10/upright10`为完整角色/重力朝向的十分之一度，正数顺时针，`angvel10`为角速度的十分之一度/秒；`stretch/shear`为-100到100的弹性拉伸/侧弯，`contact`为0到100的接触受力强度；`imu_us`/`render_us` 是最近一次采样/完整绘图耗时（微秒）。传感器不可用时保留讲话眼珠动画及原有输入功能。轴交换参考[官方出厂 IMU HAL](https://github.com/m5stack/M5StopWatch-UserDemo/blob/main/main/hal/hal_imu.cpp)。

三份刷写脚本都会先将本次 ELF、固件、分区表和 bootloader 按 ELF SHA-256 归档到本地 `reports/firmware-archive/`。分析崩溃记录必须匹配当次 ELF；重编译后的符号即使来自相近源码，也不能当作原固件的精确定位。这些文件可能包含配对密码，不公开上传。

诊断串口每秒输出 `VIBE` 状态：采样计数、电平、按键、运行时间、重启原因及错误计数。v5 还包含 BOOT 引脚 `g0`、黄/蓝按键累计次数 `presses`、电源按键状态 `pm_btn` 和配置 `pm_cfg`，用于调查按黄色键后黑屏。v6 增加电源寄存器 `pm_pwr` 和 LED 配置读回校验 `led_cfg_ok`；v7 增加 PMIC 标识 `pm_id`、第二按键配置 `pm_cfg2`、GPIO 复用/驱动/输入 `pm_func`/`pm_drv`/`pm_io`，以及本轮读取是否成功的 `pm_valid`。`P` 返回 `FRAME 466 466 434312` 后接 RGB565 大端屏幕帧；C480接收器为320×240，GEEK为240×135。`tools/capture_frame.py` 按响应尺寸解码并输出PNG。

维护命令必须成对发送完整行，两步间隔不超过 2 秒。进入 ROM 下载模式使用 `VIBE/1 ARM-BOOT 288485439560\n` 与 `VIBE/1 CONFIRM-BOOT 288485439560\n`。通过 M5PM1 重启整机使用 `VIBE/1 ARM-RESET 288485439560\n` 与 `VIBE/1 CONFIRM-RESET 288485439560\n`；仅在显式收到这组命令时执行一次，不会每次启动自动重启。两种确认不能混用，单字节 `B` 不再触发复位。后者会短暂断开 USB，适用于排查仅重启 ESP32 后仍保留的 PMIC 状态。

重复黑屏诊断另外使用 M5PM1 的32字节 RTC RAM（0xA0–0xBF），按[官方手册](https://m5stack-doc.oss-cn-shenzhen.aliyuncs.com/1207/M5PM1_Datasheet_CN.pdf)可跨ESP主芯片掉电保留，PMIC自身掉电则不保证。两个16字节CRC槽交替保存约每秒的按键原值、配置、电源、唤醒、IRQ3、GPIO0、USB、黄色键、亮屏与维护意图；写后读回一致才推进，连续三次写入失败停写至下次启动。只写RTC RAM，不写Flash或电源控制寄存器。启动先保存上一轮有效槽，再开始新记录；这补充原有ESP RTC记录，不是故障自动修复。

`pm_irq`/`pm_mask`低到高字节分别是IRQ1/2/3状态及掩码，仅读取、不清除、不改变中断使能；`pm_irq_ok`为本轮读取有效。`pm_diag=启用/成功写入数/错误数`，`pm_save_ms`为本轮最近成功记录时刻。`pm_prev=有效,序号,运行毫秒,按键,配置,唤醒,电源,标志,IRQ3`，后六项为十六进制；标志bit0–1为维护意图（0正常、1维护下载、2维护重启），bit2为GPIO0，bit3为USB，bit4为黄色键，bit5为亮屏。维护下载最多等待500ms让主线程保存标志；记录失败不阻止恢复入口。`pm_btn=01`曾在用户明确未按红键时持续存在，不将它或`pm_hold`直接解释为实际长按。

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
