---

title: chariot — ESP32 投石小车
type: project-index
status: 原型 / 子系统控制状态见固件说明
mcu: ESP32-D0WD-V3
fqbn: esp32:esp32:esp32
serial_port: COM9
last_verified: 2026-10-18
last_updated: 2026-10-18
---

# chariot — ESP32 投石小车

> **2026-10-18最新状态：** 板上固件为 **v3.6-HOLDUD**（COM9）：**按住右侧正三角 △ = 机构上升、按住叉 × = 下降**，80% 占空比、松手即停、单次上限 3000ms；R1 仍为交替方向的定长点按（+1 方向 3000ms、−1 方向 1750ms）。左侧十字键驾驶不受影响（△/× 在右侧，是不同的按键位）。**方向已由用户现场确认为正确：上升 = 原始 +1（GPIO25/FEED_IN1），`FEED_UP_DIRECTION=1` 生效，无需改极性。** 867 项主机检查、编译与烧录校验通过。[v3.6按住说明](docs/logs/2026-10-18-hold-updown/README.md)。N20 已定性为启动占空比不足：30%/50% 起不动、65% 只抖动、75% 空载起转、**80% 带负载可带动机构 3 秒**（[门槛扫描](docs/logs/2026-10-18-n20-jog-100/README.md) · [带载3秒](docs/logs/2026-10-18-n20-loaded-3s/README.md)）。仍未测量 VM 端电压与电流；无电流/限位保护。以下各版本段落保留历史状态。

> **v3.5-R1LONG：** R1 交替方向且两个时长不同（+1 为 3000ms、−1 为 1750ms），827 项检查通过。[v3.5记录](docs/logs/2026-10-18-r1-direction-duration/README.md)。

> **v3.4-R1ALT（板上当前固件）：** R1 交替方向、每方向 1750ms、80%。822 项检查、编译、烧录校验通过，用户现场确认两个方向均正常。[v3.4交替说明](docs/logs/2026-10-18-r1-alternate/README.md)。

> **上一版v3.3-JOG：** 保留GPIO21控制TB3 STBY，新增一次性N20点动占空比参数。792项主机检查、编译、烧录校验及断电50%/150ms检查通过；仅N20供电、空载时，50%和70%各150ms均由用户确认“没有转动，也没有声音”，N20故障尚未解决。两台130输出线对调后，用户确认飞轮方向正常。R1默认仍为30%/150ms。[本次诊断与下一步](docs/N20短点动调试-v3.3-2026-10-18.md) · [原始记录](docs/logs/2026-10-18-n20-jog-duty/README.md) · [GPIO21改线](docs/整合程序v3.2-STBY21-2026-10-18.md)。

> **此前v3.1新接线入口与验证：** [ChariotIntegrated-v3.1-R1完整版](firmware/robot/ChariotIntegrated/README.md)，方向键驾驶、右摇杆转向、R2双飞轮、停车点按R1定时N20。当前没有舵机，GPIO27不输出脉冲；R1默认150ms/30%，行程尚待标定。v3.1修复收到方向键后触发drive frequency误判的问题，改为直接查询LEDC定时器。681项主机检查通过；底盘有动力、四轮架空测试中，用户确认四轮均响应且松手停转；130/N20本轮未通电验证。[当前操作](docs/整合程序v3-R1对接器-2026-10-18.md) · [最新检查记录](docs/logs/2026-10-18-controller-response/README.md)。以下2026-10-17段落及v3.0USB记录保留历史状态。

> 旧接线入口：[手柄驾驶 + R2 双飞轮](firmware/chassis/MecanumPS2Flywheel/README.md)仍使用六根飞轮信号，不能直接烧到已合并双EN的接线上。

> **2026-10-17 整合交付（硬件尚未改接）：** 保留L298N双130，固定方向并共用GPIO14软启动；新增TB6612接送球N20、GPIO27接仰角舵机，留下GPIO21。已提供[从拔线到接线的傻瓜式说明](docs/发射装置改接线-从拔线到验收-2026-10-17.md)、[ChariotIntegrated-v2整合程序及手柄操作](firmware/robot/ChariotIntegrated/README.md)、[详细方案](docs/抛投扩展-舵机与N20逐线改接方案-2026-10-17.md)及[软件验证](docs/logs/2026-10-17-integrated-validation/README.md)。新接线只能用匹配的新程序；本次没有烧录或运行机构，电机电流、舵机实物及电源能力仍需核实。下文保留改线前的实测状态。

一台"会跑的投石机"：**投掷机构**把球抛出去，**行驶底盘**负责开过去。另有一套让 Codex
自己探测串口、编译、烧录、读串口的工具链。

| 子系统 | 职责 | 硬件 | 代码 |
|---|---|---|---|
| **抛投装置** | 两个 130 电机各带一个摩擦飞轮，两侧**反向旋转**夹住球加速抛出 | **原驱动板 L298N + 双 130 摩擦飞轮** ← 当前方案 | [当前接线与 GPIO](docs/抛投装置-双130摩擦飞轮接线与GPIO分配.md)；[飞轮转动测试固件](firmware/catapult/FlywheelSpinTest/README.md) |
| **行驶底盘** | 四麦轮独立驱动（每轮一个 H 桥，不能并接） | **2 × TB6612FNG** 双路有刷驱动板 | [`MecanumPS2Dpad`](firmware/chassis/MecanumPS2Dpad/README.md)：方向键前后/横移，100% 目标占空比；另保留架空诊断固件 |
| **MCP 桥** | 给 Codex 提供串口/编译/烧录工具 | pyserial + arduino-cli + esptool | `tools/mcp-bridge/` |

> **2026-10-17 抛投更新（方案变更）：** 用户报告上次的发射装置方案（L298N + 双 N20 卷线蓄力）**已失败，原因是扭矩太大、电机被撕裂**，决定改用**原驱动板 L298N 驱动两个 130 电机摩擦飞轮**。旧方案的程序、主机脚本、仿真测试、接线文档与全部 2026-10-14 证据已逐字节归档（70 个方案文件 + 7 个链接目标 = **77 个文件**），逐个 SHA-256 校验：缺失 0、不一致 0：[旧方案归档](docs/archive/2026-10-17-L298N-N20卷线抛投/README.md) · [失败记录与证据边界](docs/logs/2026-10-17-卷线方案失败-电机撕裂/README.md)。新方案接线见[双 130 摩擦飞轮接线与 GPIO 分配](docs/抛投装置-双130摩擦飞轮接线与GPIO分配.md)。
>
> **当前板上固件与试射结果：** `FlywheelSpin-v3-100` 已在 COM4 烧录、写入校验及启动回读通过。本次以 `DIR 1 1`、100% 占空比、每次总时长 10 秒（含 1200ms 软启动）**连续三次小球试射成功，均由用户逐次现场确认**；三次均到时撤销输出，最终两路 DUTY、方向脚及底盘 STBY 为 0。v3 修正了满输出时 LEDC 回读 256 的判断；本机 161 项仿真检查通过。[三次试射与原始证据](docs/logs/2026-10-17-flywheel-three-shots/README.md) · [测试程序索引](docs/测试程序索引-2026-10-17.md)
>
> **证据边界：** 用户报告电机供电 6V，电机额定电压未知，供电测量位置与方法未记录。小球射出是现场观察；射程、角度、转速、电流、温升与装配固定强度未测量。三次短时成功不代表长期可靠性或全车验收，`DUTY` 回读也不证明实际引脚或电机端电压。前期单台不转现象保留在记录中，未据后续成功认定唯一根因。
>
> **2026-10-14 抛投更新（已终止）：** 用户报告 DRV8313 烧坏，移除旧驱动及 AS5600 编码器，改用 L298N + 两台 N20，采用 12V 动力方案；同日实机试验四项失败（电池像是断路、3D 打印孔过大滑齿、卷线轴无半月形杆导致齿轮脱落、人力满行程下抛射角仍接近水平）。该方案随后因**扭矩过大把电机撕裂**而终止，接线全文、程序与证据已归档：[旧 FOC 归档](docs/archive/2026-10-14-DRV8313-AS5600/README.md) · [2026-10-14 四项失败记录](docs/logs/2026-10-14-throw-test/README.md)。
>
> **2026-10-10 底盘更新：** 新增独立方向键固件 [`MecanumPS2Dpad`](firmware/chassis/MecanumPS2Dpad/README.md)，开机松开全部按键、通信有效后自动就绪，直接按方向键移动，无需 START/L1。松键撤销输出；投掷输出保持禁用。100% 版本已编译、烧录并通过动力关闭时的启动检查，带载速度、持续运行及无线断联表现仍待实车确认。

---

## 1. 目录地图（按用途）

```
chariot/
├── README.md                  ← 总入口（本文件）；各模块另有自己的 README.md，见 §9.1
├── firmware/                  固件源码，按子系统分
│   ├── catapult/              摩擦飞轮测试、已停用的 N20 与旧 FOC 固件
│   │   ├── FlywheelSpinTest/  【当前测试】双 130 飞轮软启动转动，一次/上电
│   │   ├── N20Motor2DirectionCheck/ 【已停用】只动2号、正转3秒
│   │   ├── N20Motor2Reverse10s/ 【已停用】只动2号、逆时针10秒
│   │   ├── N20SequenceTest/   【已停用】2号3秒→1号0.5秒
│   │   ├── PS2Catapult/       【已停用】旧 PS2 + FOC 抛投固件
│   │   ├── Stage1_AS5600Test/ 【已停用】旧编码器诊断
│   │   └── Stage2_FOCClosedLoop/ 【已停用】旧 FOC 闭环空载测试
│   └── chassis/               底盘固件
│       ├── MecanumPS2Dpad/    【方向键行驶】前后/左右平移，100% 目标占空比
│       ├── MecanumPS2/        【底盘主线】两线法麦轮 + PS2（本仓库基线 c762494）
│       ├── MecanumPS2*Diagnostic/  4 个架空限时诊断固件，见 docs/diagnostics/2026-10-07/
│       ├── MotorLinkCheck/    ⚠️ 奇果派 PCA9685：链路自检（不适用 TB6612）
│       └── MotorDriver/       ⚠️ 奇果派 PCA9685：简易驱动（不适用 TB6612）
├── host/                      上位机 Python（跑在电脑上）
│   ├── jog_loop.py            【排查用】连续点动底盘某一轮，让 TB6612 输出
│   │                          近似持续，便于配万用表实测 STBY/IN/VM/OUT
│   ├── flywheel_spin_once.py  【抛投·当前】核对前置状态后单次执行双飞轮软启动转动
│   ├── motor2_forward_once.py 【抛投·已停用】单次执行2号电机3秒正转（旧 N20 方案）
│   ├── motor2_counterclockwise_once.py 【抛投·已停用】2号逆时针10秒（旧 N20 方案）
│   ├── n20_sequence_once.py   【抛投·已停用】双 N20 顺序单次（旧 N20 方案）
│   ├── motion_sequence.py     【诊断】依次跑命名麦轮组合，限时 + 间隔静止核查
│   ├── pulse_once.py          【诊断】单次限时点动（单轮 500ms / ALL 300ms）
│   ├── verify_*_vm_off.py     【诊断】VM 关闭下的解析/时限/互锁验证（4 个）
│   ├── README-diagnostics.md  ↑ 诊断脚本的复现说明与固件配对表
│   ├── motor_link_check.py    奇果派底盘链路检测 + 电机点动
│   ├── serial_test.py         通用串口采集（固定截止时间，不会卡住）
│   ├── serial_watch.py        串口观察，供 flash-catapult.ps1 内部调用
│   ├── i2c_pin_sweep.py       扫多组 I2C 引脚，找 AS5600 在哪两个 GPIO 上
│   ├── ocr_photos.py          实物照片 OCR（丝印 → 文字）
│   ├── ocr_driverboard.py     驱动板照片增强 OCR（这样读出 DRV8313 的）
│   └── requirements.txt
├── tools/                     构建 / 烧录脚本
│   ├── flash-catapult.ps1     旧 FOC 工具，仅保留追溯，不适用 L298N
│   ├── build-chassis.ps1      底盘：编译 / 烧录两个固件
│   └── mcp-bridge/            Codex 的 MCP 服务器
├── tests/                     主机侧单元测试（跑在电脑上，不上板）
│   ├── test_mecanum_drive_math.cpp  麦轮混控与斜坡，对应 MecanumPS2/DriveMath.h
│   ├── test_flywheel_spin_firmware.cpp 【抛投·当前】FlywheelSpinTest 的仿真测试（本次 161 项检查）
│   ├── test_n20_motor2_firmware.cpp 【抛投·已停用】N20Motor2DirectionCheck v2 的仿真测试
│   ├── test_n20_motor2_reverse_firmware.cpp / test_n20_sequence_firmware.cpp 【已停用】
│   ├── test_dpad_firmware.cpp / test_motion_firmware.cpp / test_startup_firmware.cpp
│   ├── test_startup_host.py / test_motion_compensation_host.py / test_ramp_trace.py
│   ├── test_pulse_profile.cpp / test_wheel_compensation.cpp
│   └── run-dpad-tests.cmd    方向键固件测试的批量入口
├── docs/                      文档与资料
│   ├── 抛投装置-双130摩擦飞轮接线与GPIO分配.md  ★ 当前抛投接线
│   ├── 抛投装置-L298N双N20接线与GPIO分配.md     【已停用】退休记录，全文在归档快照
│   ├── 测试程序索引-2026-10-17.md               ★ 当前测试程序索引
│   ├── 测试程序索引-2026-10-14.md               【已归档】旧方案测试程序索引
│   ├── archive/2026-10-17-L298N-N20卷线抛投/    旧 N20 卷线方案 77 文件快照 + manifest
│   ├── archive/2026-10-14-DRV8313-AS5600/      旧 FOC 方案与证据快照
│   ├── 麦轮TB6612接线与面包板排线.md      ★ 底盘接线（当前方案，接线的唯一依据）
│   ├── 麦轮TB6612接线-两线法备选方案.md   该方案的来源、取舍与代价论证
│   ├── 投掷机构-设计与安全.md     ↓ 以下三份是重组前的原 README，
│   ├── 底盘链路检测与驱动.md      ｜ 细节已并入本文件，保留作详细参考
│   ├── Codex串口MCP桥.md         ↑
│   ├── ESP32-2804-FOC-guide.md   原始接线与烧录指南
│   ├── 诊断记录-第一阶段.md       等 5 份为实测记录（写于重组前）
│   ├── 环境检查报告.md
│   ├── 线路检查记录.md
│   ├── 烧录测试记录.md
│   ├── 阻塞问题与烧录实测.md
│   ├── PS2遥控实现与调试.md
│   ├── TT两线电机与麦轮分层验收.md / TT电机测试方案.md
│   ├── 麦轮TB6612分步验收记录-2026-10-06.md / 全车连接检验-GPIO与降压端子索引.md
│   ├── 排错对话log.md
│   ├── diagnostics/2026-10-07/  ★ TB6612 四轮排错索引 + 9 份过程记录（本批新增）
│   ├── assets/                8 张实物照片 + OCR 文本 + 面包板.xlsx
│   └── logs/                  2026-10-06 那批 16 份 + 2026-10-07/ 48 份原始串口日志
│       ├── 2026-10-17-双130飞轮转动测试/      ★ 本次编译/烧录/两次 30% 转动 + 哈希
│       ├── 2026-10-17-卷线方案失败-电机撕裂/  ★ 旧方案失效报告（用户口述，无仪器数据）
│       ├── 2026-10-14-n20-motor2-check/       【已停用方案】2号单次3秒 + 6次复现 + 哈希
│       ├── 2026-10-14-controller-wheel-check/ ★ 手柄与四轮现场检查（5 份串口采集）
│       └── 2026-10-14-throw-test/             实机抛投四项失败记录（旧方案，已随批归档）
└── build/                     构建产物，已 gitignore（可重新生成）
```

## 2. 实测硬件事实（不是猜的）

```
串口   : COM4  USB-SERIAL CH340  (USB ID 1a86:7523)   ← 本次枚举与烧录端口
芯片   : ESP32-D0WD-V3 rev3.1  = ESP32-WROOM-32E 那档   Flash 4MB   MAC 08:a6:f7:a8:95:94
FQBN   : esp32:esp32:esp32      (ESP32 Dev Module)
```

> 2026-10-17 本次实测端口为 **COM4**，芯片与 MAC 同历史记录一致。历史日志保留 COM9/COM10 原值；其他电脑运行前应重新枚举端口，不直接照抄旧端口号。

> ⚠️ **接线照片上写的主控是 ESP32-S3，插在电脑上的实测是经典 ESP32。**
> 两者 FQBN 不同，选错会烧出跑不起来的固件。`flash-catapult.ps1` 的做法是先用 esptool
> 问芯片、拿实测结果决定编译目标，所以这个矛盾不会导致烧错；**但别照 S3 的示意图去插排针**。

### 抛投装置（当前方案）：原驱动板 L298N + 双 130 摩擦飞轮

移除 DRV8313 与 AS5600 后释放 **GPIO14、21、22、25、26、27，共 6 个**，方案变更后**没有增减 GPIO**。PS2 保留原四线接法。

| L298N 信号 | GPIO | 用途 |
|---|---|---|
| ENA | 14 | **130-1 左飞轮 PWM / 软启动**（5kHz） |
| IN1 / IN2 | 25 / 26 | 130-1 方向 |
| ENB | 27 | **130-2 右飞轮 PWM / 软启动**（5kHz） |
| IN3 / IN4 | 21 / 22 | 130-2 方向 |

OUT1/OUT2 接 130-1（左），OUT3/OUT4 接 130-2（右）。取下 ENA/ENB 常高跳帽，两个 EN 各加 10kΩ 下拉。逻辑 5V 与动力分段照旧；底盘保留适配其电机的独立动力分支。GPIO21 的旧 SDA 线束故障需排除后再使用。

与旧 N20 方案的区别只有两点：**EN 从「常高开关」改为 PWM 软启动脚**，以及**负载从卷线/齿轮机构改为飞轮惯性负载**。当前 v3 测试固件占空比上限 100%、默认 30%，单次不超过 10 秒，每次上电只允许一次；电机额定电压、启动电流与 L298N 温升仍未测量，短时试射成功不能替代这些参数。

完整电源、跳帽、电流风险与安全说明见[当前接线文档](docs/抛投装置-双130摩擦飞轮接线与GPIO分配.md)。当前只实现双侧同占空比软启动测试；已取得三次小球试射成功的现场反馈，独立转向记录、射程、电流/温升及底盘整合与互锁仍待完成。

PS2 的 GPIO2/12 属启动配置脚，现有接线仍需核对冷启动；18/23 已用于底盘，不可挪作 PS2。

### 行驶底盘（当前方案）：2 × TB6612FNG，两线法

**四麦轮必须四路独立控制，不能两轮并接。** 一块 TB6612FNG 只有 2 个 H 桥，所以用两块（D1 左板、D2 右板）。

| 设备 / 车轮 | IN1 / IN2 | 说明 |
|---|---|---|
| D1-A 左前 FL | **16 / 17** | `PWMA`/`PWMB` 四个脚**全部接 3.3V**，不占 GPIO |
| D1-B 左后 RL | **18 / 19** | 这是"两线法"：PWM 脚常高，用两个 IN 脚打 PWM |
| D2-A 右前 FR | **23 / 32** | |
| D2-B 右后 RR | **15 / 5** | 两个启动配置脚，**启动态必须实测** |
| 两板共用 STBY | **33** ＋ **10kΩ 下拉到 GND** | **强制项**：不装则上电瞬间可能四轮全速 |
| 两板 VCC | ESP32 3V3 | **不要接 5V**，否则 3.3V 高电平可能不被接受 |

**底盘共 9 根输出**，与 PS2 四根及新 L298N 六根信号无重叠。完整全车分配见新抛投接线文档。

- **方向键行驶固件：** [`MecanumPS2Dpad`](firmware/chassis/MecanumPS2Dpad/README.md) 沿用此接线，轮向极性为 `{1,1,-1,-1}`，已完成编译、烧录和启动检查，100% 版本尚未进行带动力实测。`firmware/chassis/MecanumPS2*Diagnostic/` 是**架空诊断**固件
  （只有限时点动，用来实测链路与命令解析）；`firmware/chassis/Motor*/` 是奇果派 PCA9685 的代码，烧进 TB6612 什么都不会发生。
  实现要点（启动先拉低 STBY、换向先撤原方向 PWM、制动要让 PWM 外设真的输出常高）见接线文档 §3。
- ⚠️ **9 根引脚用满，没有舵机信号脚了。** 投球机构若要 MG995 舵机，需改用下面的备选方案。
- 完整接线表、面包板孔位、验收步骤：**`docs/麦轮TB6612接线与面包板排线.md`（接线以此文为准）**。

### 行驶底盘（备选）：奇果派 QGPMaker PCA9685

| 项目 | 值 |
|---|---|
| 驱动板 | 奇果派 QGPMaker，**PCA9685 @ I2C `0x60`**（只占 2 根 IO，与开发板型号无关） |
| 能力 | 4 路直流电机 M1–M4 / 2 路步进 / **8 路舵机** ← 需要舵机时选它 |
| ⚠️ 限制 | **编码器功能仅支持 Arduino UNO**；本项目电机不带编码器，正好绕开 |
| ⚠️ 代价 | **依赖 I2C（GPIO21/22）—— 也就是当前有间歇 SDA 故障的那两根线** |
| 供电 | 逻辑电来自开发板 USB；**动力必须外接 6–12V 到 DC 口**，否则"I2C 通了但电机不动" |
| 引脚 | ESP32：SDA=21 SCL=22；UNO：SDA=A4 SCL=A5 |
| 固件 | `firmware/chassis/` 现成，且 `host/motor_link_check.py` 可直接用 |

## 3. 快速开始

所有命令都在**仓库根目录**下执行（仓库路径含空格和中文，脚本内部已用 `$PSScriptRoot` 相对解析）。

**抛投装置（双 130 摩擦飞轮）** —— 当前固件为 `FlywheelSpin-v3-100`，本次串口 COM4。[三次试射记录](docs/logs/2026-10-17-flywheel-three-shots/README.md)保留编译、烧录、源码哈希和运行日志。编译时将源码复制到英文目录，并按本机安装位置设置 CLI、配置文件及构建路径；下列为可调整的模板，运行前先按[接线文档](docs/抛投装置-双130摩擦飞轮接线与GPIO分配.md)完成准备：

```powershell
# 1) 将路径改为本机 CLI 配置及英文源码/构建目录
$config = 'D:\Temp\chariot-flywheel\arduino-cli.yaml'
$sketch = 'D:\Temp\chariot-flywheel\FlywheelSpinTest'
$build = 'D:\Temp\chariot-flywheel\build'
arduino-cli compile --config-file $config --fqbn esp32:esp32:esp32 --build-path $build $sketch
# 2) 烧录（烧录时动力侧应断开）
arduino-cli upload --config-file $config --fqbn esp32:esp32:esp32 `
  --port COM4 --input-dir $build --upload-property upload.speed=115200 $sketch
# 3) 准备并确认可运行后，再单独执行：核对状态 -> DIR -> 准备 -> 一次 SPIN -> STOP
# 日志使用新文件名；每次上电仅一次，脚本不会自动复位或重试
python host\flywheel_spin_once.py --port COM4 --log logs\flywheel-new-run.txt --duty 30 --ms 3000 --prepare 10
```

原 `flash-catapult.ps1` 的 Stage1/Stage2 为旧 FOC 工具，不适用 L298N；旧 N20 的固件、主机脚本与仿真测试均已归档。

**行驶底盘 · 奇果派 PCA9685（备选方案）** —— 编译 / 烧录。
下面这套只适用于奇果派板；当前 TB6612 使用上述独立底盘固件：

```powershell
python -m pip install -r host\requirements.txt          # 一次性
.\tools\build-chassis.ps1 -ListBoards                    # 看板子插在哪个串口
.\tools\build-chassis.ps1 -Sketch MotorLinkCheck -Upload # 先烧链路自检
python host\motor_link_check.py                          # 跑检测，直接给结论
.\tools\build-chassis.ps1 -Sketch MotorDriver -Upload    # 确认通信后再烧驱动
python host\motor_link_check.py --drive 1 200            # M1 正转 200
python host\motor_link_check.py --stop
```

**MCP 桥**（让 Codex 自己烧录）—— 已注册在 `~/.codex/config.toml` 的 `[mcp_servers.arduino]`：

```powershell
python tools\mcp-bridge\mcp_server.py --selftest   # 本地跑一遍每个工具
python tools\mcp-bridge\test_mcp.py                # 验证 MCP 协议
```

## 4. 供电与运动边界

- 本次用户报告抛投电机供电 **6V**，测量位置与方法未记录，电机额定电压仍未知。旧方案的 12V 设置不能直接视为当前供电或 130 电机额定值；底盘动力也应按其电机规格配置，现车 VM 以实测为准。
- L298N 的电机动力与 5V 逻辑供电分开配置；外接 5V 前按实际模块说明隔离板载稳压器，ENA/ENB 跳帽与 5V 稳压跳帽不能混淆。
- 两个 EN 各装 10kΩ 下拉，动力关闭时验证上电/复位默认禁用；各模块共地，电机动力与回流使用合适的电源线。**ENA/ENB 常高跳帽没取下来时，固件会把空闲引脚读成高电平、锁 `hw=0` 并拒绝运行**——这是接线问题，不能靠固件弥补。
- **飞轮是惯性负载，启动电流接近该电压下的堵转电流**；L298 每路 2A 是直流绝对最大额定值，不是长期能力。首次带载必须限流、低占空比起步，并同时监视电流与散热片温度；超限就要重新评估驱动板。
- **飞轮是储能件**：断电后仍会旋转一段时间，禁止用手或物体接触旋转中的飞轮。动力侧要有物理断电开关，不要只靠串口 `STOP`。
- 旧 FOC 固件会操作已重新分配的引脚，不可用于 L298N。USB 调试仍隔离降压到 ESP32 的正极线。

## 5. 抛投装置的工作流

新方案的机械职责：两个 130 电机各带一个摩擦飞轮，分别装在球道两侧，**两侧反向旋转**夹住球并把球加速推出。测试固件让两侧以同一占空比同时软启动（0 → 目标，1200ms 线性爬升）、保持指定时长、到时或 `STOP` 立即撤销两路 EN；每次上电只允许一次，上电静止。

**2026-10-17：方案变更。** 上一版方案（L298N + 双 N20 卷线蓄力：2号转轴拉绳蓄力、1号控制2号位置实现释放）**已失败，原因是扭矩太大、电机被撕裂**。此前 2026-10-14 的实机试验还记录过四项失败（电池像是断路、3D 打印孔过大滑齿、卷线轴无半月形杆导致齿轮脱落、人力满行程下抛射角仍接近水平）。两次结论都是**用户现场口述、没有仪器数据**：[失效报告](docs/logs/2026-10-17-卷线方案失败-电机撕裂/README.md) · [四项失败记录](docs/logs/2026-10-14-throw-test/README.md)。

工程理由：旧方案把电机经齿轮与卷线轴硬联到拉绳和限位端，机构一旦到位或卡住就进入堵转/冲击工况，反作用于电机的力矩上限由**机构**决定——这是「电机被撕裂」的力学来源。飞轮方案把能量存进转子动能，负载是惯性负载，没有硬限位反冲，力矩由电机自身特性决定，从原理上避开该失效模式；代价是风险转移到**轮毂紧固、动平衡**与**启动电流/驱动板能力**。

**当前状态：** v3 已编译、烧录，161 项主机仿真通过；在 100% / 10000ms 下，**连续三次小球试射成功（用户现场确认）**，三次均到时归零。装配固定强度、独立转向记录、射程、启动电流与温升尚未测量。见[三次试射记录](docs/logs/2026-10-17-flywheel-three-shots/README.md)；[早期测试](docs/logs/2026-10-17-双130飞轮转动测试/README.md)作为历史保留。

旧 PS2 蓄力 / FOC 轨迹 / 校准流程完整保存于[旧 FOC 归档](docs/archive/2026-10-14-DRV8313-AS5600/README.md)；旧 N20 卷线方案的程序与证据保存于[旧 N20 归档](docs/archive/2026-10-17-L298N-N20卷线抛投/README.md)。

## 6. 当前阻塞

| 项目 | 待完成内容 |
|---|---|
| 飞轮机械（2026-10-17 新增） | 飞轮与电机轴必须**周向+轴向同时固定**（D 形切面/紧定螺钉/夹紧轮毂，至少两处），加防松、做动平衡，确定球道间隙与压入量，固定电机座并加护罩。旧方案的滑齿与「齿轮沿圆轴脱出」是明确的反面案例 |
| 130 规格与驱动能力（2026-10-17 新增） | 记录两台 130 的铭牌与额定电压；首次带载实测启动电流与 L298N 散热片温升。L298 每路 2A 是绝对最大额定值，不是长期能力；若实测超限需重新评估驱动板 |
| 对转极性（2026-10-17 新增） | 现场观察两轮转向，确认两侧**对转**方向并固定接线；`d1=1 d2=1` 目前只是实验值，不能写成已确认 |
| 抛投供电（2026-10-14 新增，未解决） | 用户报告电池像是断路，整机只能直连电源；损坏环节（电芯/保护板/接插件）未定位，也未测电压与导通。属现场判断，不是实测参数 |
| 加球试射与几何（2026-10-17 更新） | 目标城墙高度与距离仍未记录；球道间隙/压入量与占空比上限需按试射结果调整，并重新验算抛射角 |
| 接线验收 | 确认动力与 5V 逻辑分支、共地、ENA/ENB 跳帽与 10kΩ 下拉，排除原 GPIO21 线束异常 |
| 底盘整合与互锁 | 两路飞轮 PWM 的 LEDC 通道/定时器要与底盘八路统一分配（本测试固件用通道 0/1 是因底盘全程禁用）；行驶与发射动作加互锁 |
| 机械急停 | 物理断电开关的位置与可达性；飞轮旋转面的隔离与警示 |

旧 AS5600 通信/磁场/FOC 校准阻塞属于已停用方案，见[旧 FOC 归档](docs/archive/2026-10-14-DRV8313-AS5600/README.md)；N20 卷线机构的相关阻塞（打印孔配合、半月形杆、抛射几何）属已终止方案，见[旧 N20 归档](docs/archive/2026-10-17-L298N-N20卷线抛投/README.md)。

### 各子系统验证状态

| 子系统 | 状态 |
|---|---|
| 抛投 · L298N + 双 130 摩擦飞轮（当前） | v3 编译、烧录（COM4）及 161 项主机仿真通过；100% / 10s **连续三次试射成功，用户逐次确认**。日志均到时归零；射程、独立转向、电流/温升、装配强度及长期可靠性未测量 |
| 抛投 · L298N + 双 N20 卷线（已终止） | 2026-10-17 因**扭矩过大把电机撕裂**而终止，改用摩擦飞轮；77 个文件已归档并逐个 SHA-256 校验（缺失 0、不一致 0） |
| 旧抛投 · DRV8313 / AS5600 / FOC | 2026-10-14 停用，文档、固件与实测证据已归档 |
| 行驶底盘 · 奇果派 PCA9685（备选） | 2 个固件 × 5 种开发板**编译全部通过**；**尚未实机烧录**；该方案已让位给 TB6612 |
| 行驶底盘 · TB6612FNG（当前） | 接线按两线法装配；方向键固件 `MecanumPS2Dpad` 已编译、烧录，100% 目标占空比，启动时手柄在线且四轮输出零。100% 版本带载运动仍待确认。历史单轮双向及四轮 300ms 短测通过现场观察，持续联动异常仍保留于诊断记录 |
| MCP 桥 | MCP 协议层与 Codex 端到端**均已验证** |

## 7. 故障排查

| 现象 | 先查 |
|---|---|
| 找不到串口 | 数据线（有些只能充电）、CH340/CP210x 驱动、板子插好没 |
| 串口被占用 / `Access is denied` | 关掉 Arduino IDE 的串口监视器 |
| 上传卡在下载阶段 | `-UploadSpeed 115200` 降速；必要时手动按 BOOT/EN |
| 底盘 I2C 检测 PASS 但电机不转 | 十有八九**没接动力电源（VM）**——逻辑电来自 USB，电机要 6–12V |
| 底盘检测 FAIL，总线上一个器件都没有 | 驱动板插到底没 / 插歪；驱动板电源灯；烧的是不是本项目固件；试 `--raw "I2C 21 22"` |
| **TB6612：上电瞬间四轮就转** | **STBY 没装 10kΩ 下拉**（强制项），逐个确认两板 STBY 都真的被拉到低 |
| **TB6612：方向线怎么动都不转** | 该轮 `PWMA`/`PWMB` 没接到 3.3V —— PWM 脚为低时是**一直短路制动**，不是没电 |
| **TB6612：某轮只能转一个方向** | 该轮两个 IN 脚之一虚接；两线法两个脚都要到位 |
| **TB6612：冷启动或下载异常** | GPIO15 / GPIO5 是启动配置脚，按接线文档实测启动态与下载 |
| 130 飞轮不转或明显发热 | 断开动力，检查 ENA/ENB 跳帽是否取下、方向电平、实际输出电压与电流；固件 `hw=0` 时先解决接线 |
| 飞轮启动瞬间电流很大 / L298N 发烫 | 飞轮是惯性负载，启动电流接近堵转电流；降低占空比、限制单次时长，检查散热与电源能力 |
| `SPIN` 被拒绝（`ERR SPIN requires ...`） | 先 `DIR <d1> <d2>` 设极性；每次上电只允许一次，执行过就需复位重启 |
| 端口从 COM9 变了 | 重新确认实际端口号（本次为 COM4），同一块板 MAC 不变 |
| ESP32 重启 | 供电跌落、共地、电流回路、接触不良、电机干扰 |

## 8. 环境与依赖

| 项目 | 值 |
|---|---|
| Arduino | 本次使用 `D:\arduino\Arduino IDE` 内置 CLI，上传工具 esptool 5.3.1；早期日志中的 `D:\Download\arduino` 为历史路径 |
| ⚠️ 坑 | 按实际安装配置 CLI 的数据目录；本次用独立 `--config-file` 指向 `C:\Users\33920\AppData\Local\Arduino15`，并在英文目录编译。旧脚本硬编码路径不代表本机当前路径 |
| 核心 / 库 | `esp32:esp32` **3.3.12**、`Simple FOC` **2.4.0**（库名有空格：`arduino-cli lib install "Simple FOC"`） |
| Python | `pyserial` 3.5、`mcp` 2.3.0（用了 v2 的 `MCPServer` API）、`rapidocr-onnxruntime`、`opencv-python` |
| MCP 注册 | `~/.codex/config.toml` → `[mcp_servers.arduino]`，含 `PYTHONUTF8=1`（否则 Windows 中文乱码） |

## 9. 文档索引

> **接底盘请先看 §9.1 与 §9.2 的前两行**；其余为投掷机构、工具链与历史记录。

### 9.1 模块 README（与总入口互相链接）

根 `README.md` 是**总入口**。下列**每个 sketch / 诊断目录各带自己的 `README.md`**，其顶部都有返回总入口的导航块，模块之间也互相链接。

**抛投机构（当前方案：双 130 摩擦飞轮）**

当前测试入口：[`FlywheelSpinTest`](firmware/catapult/FlywheelSpinTest/README.md)：v3 两侧飞轮以同一占空比软启动转动（默认 30%、上限 100%、单次 ≤10s），每次上电只允许一次，底盘禁用。[连续三次试射成功记录](docs/logs/2026-10-17-flywheel-three-shots/README.md)。

**旧投掷机构（已停用，历史模块）**

旧 N20 卷线测试入口（已停用）：`N20Motor2DirectionCheck`（2号正转3秒）、`N20Motor2Reverse10s`（2号逆时针10秒）、`N20SequenceTest`（2号3秒→1号0.5秒）。源码保留在原目录用于追溯，README 已标停用，逐字节快照见[旧 N20 归档](docs/archive/2026-10-17-L298N-N20卷线抛投/README.md)。`PS2SignalCheck`（遥控信号只读诊断）不属于旧方案专用件，仍可继续使用。

| 模块 README | 内容 |
|---|---|
| [`firmware/catapult/N20Motor2DirectionCheck/README.md`](firmware/catapult/N20Motor2DirectionCheck/README.md) | 【已停用】仅2号正转3秒：引脚、命令、GPIO 回读互锁与一次/上电 |
| [`firmware/catapult/N20Motor2Reverse10s/README.md`](firmware/catapult/N20Motor2Reverse10s/README.md) | 【已停用】仅2号逆时针10秒 |
| [`firmware/catapult/N20SequenceTest/README.md`](firmware/catapult/N20SequenceTest/README.md) | 【已停用】2号3秒→1号0.5秒的两阶段顺序与四种极性 |
| [`firmware/catapult/PS2Catapult/README.md`](firmware/catapult/PS2Catapult/README.md) | 【已停用】旧 PS2 遥控 + 蓄力 + 90° 往返：引脚、命令、配置锁、安全红线 |
| [`firmware/catapult/Stage1_AS5600Test/README.md`](firmware/catapult/Stage1_AS5600Test/README.md) | 第一阶段：只读编码器 + I2C 电气诊断（`G`/`W`/`L`/`P` 命令） |
| [`firmware/catapult/Stage2_FOCClosedLoop/README.md`](firmware/catapult/Stage2_FOCClosedLoop/README.md) | 第二阶段：闭环空载测试，与解除配置锁前的核对清单 |

**行驶底盘（TB6612 两线法，当前方案）**

| 模块 README | 内容 |
|---|---|
| [`firmware/chassis/MecanumPS2Dpad/README.md`](firmware/chassis/MecanumPS2Dpad/README.md) | 【方向键行驶】前后/左右平移、100% 目标占空比、测试和启动日志 |
| [`firmware/chassis/MecanumPS2/README.md`](firmware/chassis/MecanumPS2/README.md) | 【底盘主线】两线法麦轮 + PS2：命令表、占空比/时限、安全边界 |
| [`firmware/chassis/MecanumPS2MotionDiagnostic/README.md`](firmware/chassis/MecanumPS2MotionDiagnostic/README.md) | ★ 当前诊断版：4 个诊断固件的版本/极性/命令总表 + `MOVEPULSE` |
| [`firmware/chassis/TB6612LogicCheck/README.md`](firmware/chassis/TB6612LogicCheck/README.md) | 第 2 步逻辑验收固件（只回读引脚，不含使能与运动） |
| [`firmware/chassis/MecanumPS2FourWheelDiagnostic/README.md`](firmware/chassis/MecanumPS2FourWheelDiagnostic/README.md) | 历史：首版装车校准（极性 `{1,1,-1,-1}`）+ `ALLPULSE` 四轮同时脉冲 |
| [`firmware/chassis/MecanumPS2WheelDiagnostic/README.md`](firmware/chassis/MecanumPS2WheelDiagnostic/README.md) | 历史：四标签单轮 `PULSE`（极性仍未校准） |
| [`firmware/chassis/MecanumPS2Diagnostic/README.md`](firmware/chassis/MecanumPS2Diagnostic/README.md) | 历史：只允许 `PULSE RL` 的单轮脉冲 |

**工具与记录**

| 模块 README | 内容 |
|---|---|
| [`host/README-diagnostics.md`](host/README-diagnostics.md) | 诊断上位机脚本：前置条件、固件配对、逐脚本用法、证据边界 |
| [`docs/diagnostics/2026-10-07/README.md`](docs/diagnostics/2026-10-07/README.md) | ★ TB6612 四轮排错索引：现场结论表、证据边界、下一步待测 |

**⚠️ 已让位的奇果派 PCA9685（仅备选，烧到 TB6612 无反应）**

| 模块 README | 内容 |
|---|---|
| [`firmware/chassis/MotorLinkCheck/README.md`](firmware/chassis/MotorLinkCheck/README.md) | 链路自检：写回读 PCA9685 寄存器；含可选 ACS712 电流检测 |
| [`firmware/chassis/MotorDriver/README.md`](firmware/chassis/MotorDriver/README.md) | 简易驱动：ASCII + 二进制双协议、看门狗、8 路舵机 |

> **覆盖情况**：`firmware/` 下 11 个 sketch 目录现已全部带 `README.md`（另有 `host/` 与 `docs/diagnostics/` 各 1 个）。

### 9.2 文档表

| 文档 | 内容 |
|---|---|
| **`docs/麦轮TB6612接线与面包板排线.md`** | ★ **底盘接线当前方案（两线法）**：全车 GPIO、四轮对应、面包板孔位表、分步验收。**取代旧三线法**，已对齐 §2.3 |
| `docs/麦轮TB6612接线-两线法备选方案.md` | 该方案的来源与论证：13 根 vs 9 根、代价（用满引脚/无舵机余量）、对 I2C 扩展器的担心 |
| [新抛投接线](docs/抛投装置-双130摩擦飞轮接线与GPIO分配.md) | ★ 当前方案：原驱动板 L298N + 双 130 摩擦飞轮，六根 GPIO、PWM 与软启动取舍、电流风险、机械与安全要求 |
| [旧 N20 接线](docs/抛投装置-L298N双N20接线与GPIO分配.md) | 【已停用】退休记录：停用时的事实、哪些内容继续有效；全文在[归档快照](docs/archive/2026-10-17-L298N-N20卷线抛投/snapshot/docs/抛投装置-L298N双N20接线与GPIO分配.md) |
| [测试程序索引 2026-10-17](docs/测试程序索引-2026-10-17.md) | ★ 当前测试程序索引：飞轮测试入口、旧方案程序归档位置、未归档部分 |
| [测试程序索引 2026-10-14](docs/测试程序索引-2026-10-14.md) | 【已归档】旧方案的测试程序保留索引 |
| [旧 N20 归档](docs/archive/2026-10-17-L298N-N20卷线抛投/README.md) | ★ 旧方案的 70 个程序/证据文件 + 7 个链接目标逐字节快照 + `manifest.json`（缺失 0、不一致 0） |
| [旧 FOC 归档](docs/archive/2026-10-14-DRV8313-AS5600/README.md) | 旧 FOC 方案的原文、代码、日志、照片与校验清单 |
| `docs/ESP32-2804-FOC-guide.md` | 【已归档】原 FOC 接线与烧录指南 |
| `docs/投掷机构-设计与安全.md` | 原 README：硬件参数、EN 极性推导、排查表、安全须知 |
| `docs/底盘链路检测与驱动.md` | 原 README：**奇果派板**识别过程、固件协议、三层检测能力、常见问题 |
| `docs/Codex串口MCP桥.md` | 原 README：9 个 MCP 工具、审批分级、管理命令、已知约束 |
| `docs/PS2遥控实现与调试.md` | PS2 蓄力投掷的交付范围、控制原理、调试顺序、还缺什么 |
| `docs/阻塞问题与烧录实测.md` | 【已归档】旧投掷状态：已完成项 + 阻塞表 + 不可远程解决项 |
| `docs/诊断记录-第一阶段.md` | AS5600 找不到的完整诊断过程（含三轮更正与决定性测法） |
| `docs/环境检查报告.md` | 本机 Arduino / Codex / 串口 检查记录与改动清单 |
| `docs/线路检查记录.md` | 五个模块（编码器/电机/降压/驱动板/蓝牙）的线路状态与待验证项 |
| `docs/烧录测试记录.md` | 串口、芯片、编译产物大小、哈希校验记录 |
| `docs/logs/2026-10-17-flywheel-three-shots/` | ★ 当前成果：v3 编译/烧录（COM4）、161 项主机仿真、100% / 10 秒连续三次试射成功（用户逐次确认）、原始日志及 SHA-256 清单 |
| `docs/logs/2026-10-17-双130飞轮转动测试/` | 历史：COM10 上早期 30% / 3000ms 命令执行日志、源码/映像哈希及证据边界；后续用户反馈当时未观察到实际转动 |
| `docs/logs/2026-10-17-卷线方案失败-电机撕裂/` | ★ 旧方案失效报告：用户口述「扭矩太大、电机被撕裂」、已知与未知、工程结论；`user-report.json` 为机器可读原件 |
| `docs/logs/2026-10-14-throw-test/` | 实机抛投四项失败记录（电池断路、滑齿、齿轮脱落、抛射角过平）与证据边界（旧方案，已随批归档） |
| `docs/logs/2026-10-14-n20-motor2-check/` | 【已停用方案】2号电机单次3秒正转、5 次追加复现、跳帽异常与源码/映像哈希 |
| `docs/logs/2026-10-14-controller-wheel-check/` | 手柄与四轮现场检查：5 份串口采集、状态统计、远程源码逐项比对 |
| `docs/logs/*.txt` | 原始串口日志：2026-10-06 那批 16 份（`stage1-*`、`ps2-*`、`connection-*`）+ `2026-10-07/` 48 份 |
| `docs/diagnostics/2026-10-07/` | 9 份 TB6612 四轮排错过程记录（接线测点、逐轮验收、麦轮组合、限时占空比） |
| `docs/assets/*.jpg` | 8 张实物照片（TB6612 引脚图、驱动板、降压模块、编码器、接线） |
| `docs/assets/面包板.xlsx` | 面包板孔位图（a–j × 1–30，两侧 +/− 轨），配合接线文档 §5 |

> **日志命名约定**：`docs/logs/` 下，单批 **≥10 份**时建 `docs/logs/<日期>/` 子目录
> （如 `2026-10-07/` 48 份）；份数少时直接平铺（如 2026-10-06 那批 16 份）。
> 历史日志一律不平铺改目录 —— 多份文档已有指向原路径的链接，移动会全部打断。
>
> 5 份实测记录写于仓库重组前，其顶部已加路径说明；文中旧路径（`D:\dsh\1001\...`）已按新布局更新。

## 10. 下一步优先级

2026-10-17 方案变更后，飞轮的**机械装配与电流实测**优先于继续调固件：

1. **记录 130 电机规格（最高优先）**：铭牌、额定电压、堵转电流；据此决定抛投动力分支是否仍用 12V。这一步决定后面所有升速试验的上限。
2. **装配飞轮与轮毂**：轴与轮毂周向+轴向同时固定（D 形/紧定/夹紧，至少两处）、防松、动平衡、电机座加固、加护罩与球道间隙调节。旧方案的滑齿与齿轮脱出是反面案例。
3. **分步升速实测**：断电核对跳帽与下拉 → 逻辑核对（`hw=1`，两路 DUTY=0）→ 单侧低速试转 → 确认两侧**对转**极性 → 限流下 30%→45%→60% 逐级升速，每级记录电流、温升、振动；超限就重新评估驱动板。
4. **抛投供电定位**：断电后用万用表测电池端电压与通断，确认断点在电芯、保护板还是接插件；在定位前不要把"电池坏"当成结论引用。
5. **加球试射与几何**：记录目标城墙高度与距离，按试射结果调整间隙/压入量与占空比上限；先手工验证角度，再交给电机。
6. **底盘整合**：把两路飞轮 PWM 的 LEDC 通道/定时器与底盘八路统一分配，加行驶与发射动作互锁；届时需要把本测试固件并入或替换为整合固件。
7. 继续验证底盘方向键固件的带载速度、持续运行及断联停车；历史联动停转问题仍需结合供电诊断排查。

## 参考

- [SimpleFOC 位置控制](https://docs.simplefoc.com/angle_loop) · [3PWM 驱动](https://docs.simplefoc.com/bldcdriver3pwm) · [I²C 磁编码器](https://docs.simplefoc.com/magnetic_sensor_i2c) · [电压模式](https://docs.simplefoc.com/voltage_torque_mode)
- [AS5600 产品页](https://ams-osram.com/products/sensors/position-sensors/ams-as5600-position-sensor) · [ESP32 GPIO 文档](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html)
- [奇果派驱动板资料](https://www.7gp.cn/archives/308) · [Arduino-PS2X 作者源码](https://github.com/madsci1016/Arduino-PS2X)
