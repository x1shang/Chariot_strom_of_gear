# ESP32-WROOM-32E 麦轮小车：PS2 方向键控制

> **导航**：[仓库总入口](../../../README.md) · [接线说明](../../../docs/麦轮TB6612接线与面包板排线.md) · [编译/烧录验证记录](../../../docs/logs/MecanumPS2Dpad-100pct-validation-2026-10-10.txt) · [串口启动日志](../../../docs/logs/MecanumPS2Dpad-upload-check-2026-10-10.txt)

打开本目录的 `MecanumPS2Dpad.ino`；两个 `.h` 文件保持在同一目录。
Arduino IDE 选择 **ESP32 Dev Module**，使用 **Arduino-ESP32 3.x**，本机验证核心为 3.3.12。无需第三方手柄库，沿用项目中已验证收包的 PS2 接收器通信。
PWM 使用 Espressif 的 [LEDC API](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/ledc.html)：`ledcAttachChannel` 分配独立通道，`ledcWrite` 按 GPIO 写入占空比。

## 操作

开机松开所有按键，连续收到 5 个有效无按键帧（约 100ms）后，串口显示 `ready=1`。此后直接按方向键即可，不需要 START 或 L1。

| 按键 | 车辆动作 |
| --- | --- |
| 上 | 前进 |
| 下 | 后退 |
| 左 | 向左平移，车头方向保持不变 |
| 右 | 向右平移，车头方向保持不变 |
| 上/下 + 左/右 | 对应斜向平移 |
| 松开方向键 | 撤销 STBY，PWM 归零，车辆滑行停止 |
| 圆圈 | 停机；须松开全部按键后才能再次操作 |

相反方向键在同一轴上抵消；摇杆、L1/L2/R2、START 不参与运动。开机或通信恢复时若一直按住方向键，车辆不会启动，须先松开全部按键。

串口波特率 **115200**，命令换行结尾：`STATUS` 查看状态；`STOP` 停机并要求松开全部按键后重新就绪。`STOP` 不是永久锁定，收到 5 个有效无按键帧会重新就绪。

## 沿用现有接线

| 对象 | ESP32 GPIO |
| --- | --- |
| 左前 FL：D1 AIN1 / AIN2 | 16 / 17 |
| 左后 RL：D1 BIN1 / BIN2 | 18 / 19 |
| 右前 FR：D2 AIN1 / AIN2 | 23 / 32 |
| 右后 RR：D2 BIN1 / BIN2 | 15 / 5 |
| 两块 TB6612 的 STBY | 33，共用 10kΩ 下拉到 GND |
| PS2 CLK / CS(ATT) / CMD / DAT | 2 / 4 / 12 / 13 |

两块 TB6612 的 VCC、PWMA、PWMB 接 3.3V，所有模块共地；VM 使用现有与电机匹配的动力电源。**本固件按原项目的两输入 PWM 接法设计，不适用于 PWM 脚另接 GPIO 的接法。** PS2 接收器沿用已验收供电，DAT 到 ESP32 的信号必须为 3.3V 电平。

轮位顺序为 FL、RL、FR、RR，`POLARITY={1,1,-1,-1}` 来自本项目 2026-10-07 四轮逐轮实测。若之后换过电机线，请重新确认极性。混控沿用原项目，要求麦轮安装与该混控匹配；从车顶看，四轮滚子方向的投影应组成 X。未实际验证本版横移方向。

## 参数与验证边界

按用户要求，默认 `DRIVE_DUTY=255`，即 100% 占空比（原为 192/255≈75.3%）；在 `.ino` 顶部修改，允许 1–255。每 10ms 增减 3 档，从静止按住约 850ms 后到达目标。没有启用旧实验中的启动冲击或轮速补偿。占空比不代表实测转速，四轮速度一致性需要进一步校准。

方向键松开时在下一次正常轮询撤销输出（通常约 20ms），采用滑行停止，不是机械瞬停。反向先降到零，再留 300ms 无输出间隔；直接松键也保留换向历史。无效 PS2 帧立即撤销输出，150ms 无新有效帧要求重新松键就绪，主循环另有 1 秒任务看门狗。

部分无线接收器在手柄断电后会持续返回旧按键的有效帧，本程序无法从这些帧判断无线断联，必须验证实际接收器的断联行为。原项目记录过持续运行时个别轮停转，因此本程序的编译/软件测试不能证明现有供电、电机或整车持续运行正常。

首次使用先关闭动力烧录，再将四轮架空，依次短按上、下、左、右检查轮向；正确后再落地测试。现有投掷 GPIO14/25/26/27 保持低，AS5600 不参与控制。

## 软件检查

2026-10-10：用户反馈移动偏慢，要求提高到 100% 占空比。已将目标输出改为 255/255，满输出方向及停机仿真测试通过；`esp32:esp32:esp32` / Arduino-ESP32 3.3.12 实际编译通过，程序 284079 字节，静态内存 22440 字节。用户确认动力关闭、USB 已接后，已烧录至当时 COM4 的 ESP32-D0WD-V3，esptool 写入校验通过。串口启动检查确认 `hw=1 ps=1 mode=41 buttons=0000 ready=1 STBY=0 duty=0,0,0,0`，自动状态输出正常。检查仅发送 STOP 和 STATUS，之后串口已关闭；100% 版本尚未进行带动力运动实测。归档启动日志为 `docs/logs/MecanumPS2Dpad-upload-check-2026-10-10.txt`。

构建目录中文路径会导致本机 ESP32 链接器报错，编译时使用英文临时目录后通过。构建产物仅保留在本地，仓库只归档源码及文本验证记录：`docs/logs/MecanumPS2Dpad-100pct-validation-2026-10-10.txt`。

在项目根目录运行 `tests\run-dpad-tests.cmd`。主机仿真测试覆盖方向键及四轮 GPIO 占空比、右侧极性、松键停机、开机按住方向键不启动、圆圈及串口 STOP、无效帧、150ms 超时、延迟轮询后禁止旧方向恢复、换向间隔及毫秒计数回绕。该测试通过不代表实车方向、无线断联或带载驱动已验证。

Windows 测试脚本使用本机 Visual Studio 18 Build Tools；其他安装环境可在 C++ 开发者命令提示符中执行 `cl /nologo /EHsc /std:c++17 /Itests\firmware-stubs /Febuild\test_dpad_firmware.exe /Fobuild\test_dpad_firmware.obj tests\test_dpad_firmware.cpp`，然后运行生成的测试程序；先创建 `build` 目录。Linux/macOS 可用 `g++ -std=c++17 -Itests/firmware-stubs tests/test_dpad_firmware.cpp -o /tmp/test_dpad_firmware && /tmp/test_dpad_firmware`。

关闭电机动力后可运行 `python host/verify_dpad_after_upload.py --port <实际串口>` 复核启动；只发送 STOP/STATUS，运行日志写入被忽略的本地 `logs/`。归档日志不会被脚本覆盖。
