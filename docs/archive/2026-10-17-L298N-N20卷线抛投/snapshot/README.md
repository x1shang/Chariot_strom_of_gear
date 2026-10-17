---
title: chariot — ESP32 投石小车
type: project-index
status: 原型 / 子系统控制状态见固件说明
mcu: ESP32-D0WD-V3
fqbn: esp32:esp32:esp32
serial_port: COM9
last_verified: 2026-10-10
last_updated: 2026-10-14
---

# chariot — ESP32 投石小车

一台"会跑的投石机"：**投掷机构**把球抛出去，**行驶底盘**负责开过去。另有一套让 Codex
自己探测串口、编译、烧录、读串口的工具链。

| 子系统 | 职责 | 硬件 | 代码 |
|---|---|---|---|
| **抛投装置** | 2号转轴拉绳蓄力；1号控制2号位置实现释放 | **L298N + 两台 N20，12V 动力方案** | [当前接线与 GPIO](docs/抛投装置-L298N双N20接线与GPIO分配.md)；[2号3秒→1号0.5秒测试固件](firmware/catapult/N20SequenceTest/README.md) |
| **行驶底盘** | 四麦轮独立驱动（每轮一个 H 桥，不能并接） | **2 × TB6612FNG** 双路有刷驱动板 ← 当前方案 | [`MecanumPS2Dpad`](firmware/chassis/MecanumPS2Dpad/README.md)：方向键前后/横移，100% 目标占空比；另保留架空诊断固件 |
| **MCP 桥** | 给 Codex 提供串口/编译/烧录工具 | pyserial + arduino-cli + esptool | `tools/mcp-bridge/` |

> **2026-10-14 抛投更新：** 用户报告 DRV8313 烧坏，移除旧驱动及 AS5600 编码器，改用 L298N + 两台 N20，采用 12V 动力方案。[旧文档、固件与证据已归档](docs/archive/2026-10-14-DRV8313-AS5600/README.md)。旧 FOC 固件不适用于新接线。
>
> **当前板上固件：** 已按用户要求恢复原`MecanumPS2Dpad`方向键底盘驱动，保留100%目标占空比。启动确认hw=1、ps=1、mode=73、ready=1，STBY=0及四轮输出为0；两台N20不参与控制。[恢复烧录记录](docs/logs/2026-10-14-restore-dpad/README.md)。刚才的测试源码与日志完整保留，入口见[测试程序索引](docs/测试程序索引-2026-10-14.md)。
>
> **⚠️ 2026-10-14 实机抛投未成功：** 用户现场试验后报告四项问题——电池像是断路、只能直连电源；3D 打印孔过大导致齿轮一直滑齿，加限位器失败；卷线轴没有半月形杆，齿轮在轴上滑动并脱落、无法装回；人力把杆拉满时抛射初始角度仍是平的，过不了城墙。四项均为现场口述、无仪器数据与照片；其中**抛射角度问题不依赖其余三项修复**，属机构几何设计问题。[失败记录与证据边界](docs/logs/2026-10-14-throw-test/README.md) · [机器可读报告原件](docs/logs/2026-10-14-throw-test/user-report.json)
>
> **2026-10-10 底盘更新：** 新增独立方向键固件 [`MecanumPS2Dpad`](firmware/chassis/MecanumPS2Dpad/README.md)，开机松开全部按键、通信有效后自动就绪，直接按方向键移动，无需 START/L1。松键撤销输出；投掷输出保持禁用。100% 版本已编译、烧录并通过动力关闭时的启动检查，带载速度、持续运行及无线断联表现仍待实车确认。

---

## 1. 目录地图（按用途）

```
chariot/
├── README.md                  ← 总入口（本文件）；各模块另有自己的 README.md，见 §9.1
├── firmware/                  固件源码，按子系统分
│   ├── catapult/              新 N20 方向测试与旧 FOC 固件
│   │   ├── N20Motor2DirectionCheck/ 【当前测试】只动2号、正转3秒
│   │   ├── PS2Catapult/       【已停用】旧 PS2 + FOC 抛投固件
│   │   ├── Stage1_AS5600Test/ 【已停用】旧编码器诊断
│   │   └── Stage2_FOCClosedLoop/ 【已停用】旧 FOC 闭环空载测试
│   └── chassis/               底盘固件
│       ├── MecanumPS2Dpad/    【方向键行驶】前后/左右平移，100% 目标占空比
│       ├── MecanumPS2/        【当前主线】两线法麦轮 + PS2（本仓库基线 c762494）
│       ├── MecanumPS2*Diagnostic/  4 个架空限时诊断固件，见 docs/diagnostics/2026-10-07/
│       ├── MotorLinkCheck/    ⚠️ 奇果派 PCA9685：链路自检（不适用 TB6612）
│       └── MotorDriver/       ⚠️ 奇果派 PCA9685：简易驱动（不适用 TB6612）
├── host/                      上位机 Python（跑在电脑上）
│   ├── jog_loop.py            【排查用】连续点动底盘某一轮，让 TB6612 输出
│   │                          近似持续，便于配万用表实测 STBY/IN/VM/OUT
│   ├── motor2_forward_once.py 【抛投】核对前置状态后单次执行2号电机3秒正转
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
│   ├── test_n20_motor2_firmware.cpp 【抛投】N20Motor2DirectionCheck v2 的仿真测试
│   ├── test_dpad_firmware.cpp / test_motion_firmware.cpp / test_startup_firmware.cpp
│   ├── test_startup_host.py / test_motion_compensation_host.py / test_ramp_trace.py
│   ├── test_pulse_profile.cpp / test_wheel_compensation.cpp
│   └── run-dpad-tests.cmd    方向键固件测试的批量入口
├── docs/                      文档与资料
│   ├── 抛投装置-L298N双N20接线与GPIO分配.md  ★ 新抛投接线
│   ├── archive/2026-10-14-DRV8313-AS5600/     旧抛投方案与证据快照
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
│       ├── 2026-10-14-n20-motor2-check/       ★ 2号电机单次3秒测试 + 6次复现 + 哈希
│       ├── 2026-10-14-controller-wheel-check/ ★ 手柄与四轮现场检查（5 份串口采集）
│       └── 2026-10-14-throw-test/             ★ 实机抛投四项失败记录（无仪器数据，见 user-report.json）
└── build/                     构建产物，已 gitignore（可重新生成）
```

## 2. 实测硬件事实（不是猜的）

```
串口   : COM9  USB-SERIAL CH340  (USB ID 1a86:7523)   ← COM7/COM8 是蓝牙虚拟串口，忽略
芯片   : ESP32-D0WD-V3 rev3.1  = ESP32-WROOM-32E 那档   Flash 4MB   MAC 08:a6:f7:a8:95:94
FQBN   : esp32:esp32:esp32      (ESP32 Dev Module)
```

> ⚠️ **接线照片上写的主控是 ESP32-S3，插在电脑上的实测是经典 ESP32。**
> 两者 FQBN 不同，选错会烧出跑不起来的固件。`flash-catapult.ps1` 的做法是先用 esptool
> 问芯片、拿实测结果决定编译目标，所以这个矛盾不会导致烧错；**但别照 S3 的示意图去插排针**。

### 抛投装置（当前方案）：L298N + 双 N20

移除 DRV8313 与 AS5600 后释放 **GPIO14、21、22、25、26、27，共 6 个**。PS2 保留原四线接法。

| L298N 信号 | GPIO | 用途 |
|---|---:|---|
| ENA | 14 | N20-A PWM / 使能 |
| IN1 / IN2 | 25 / 26 | N20-A 方向 |
| ENB | 27 | N20-B PWM / 使能 |
| IN3 / IN4 | 21 / 22 | N20-B 方向 |

OUT1/OUT2 接 N20-A，OUT3/OUT4 接 N20-B。取下 ENA/ENB 常高跳帽，两个 EN 各加 10kΩ 下拉。采用独立 12V 动力分支与合适的 5V 逻辑供电；底盘保留适配其电机的独立动力分支，现车 VM 待实测。GPIO21 的旧 SDA 线束故障需排除后再使用。

完整电源、跳帽与迁移说明见[当前接线文档](docs/抛投装置-L298N双N20接线与GPIO分配.md)。2号单次方向测试已实现，完整蓄力/释放及底盘整合待实现；旧 FOC 的 90° 定位与自动返回不再适用。

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

**抛投装置** —— 按 [L298N 双 N20 接线与 GPIO 分配](docs/抛投装置-L298N双N20接线与GPIO分配.md)准备，[2号单次方向测试](firmware/catapult/N20Motor2DirectionCheck/README.md)已烧录。原 `flash-catapult.ps1` 的 Stage1/Stage2 为旧 FOC 工具，其历史操作说明见归档。

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

- 新 N20 分支采用 12V，底盘 TT 额定 3–6V，现车 VM 以实测为准；不能把给底盘供电的 VADJ 一并升到 12V。
- L298N 的电机动力与 5V 逻辑供电分开配置；外接 5V 前按实际模块说明隔离板载稳压器，ENA/ENB 跳帽与 5V 稳压跳帽不能混淆。
- 两个 EN 各装 10kΩ 下拉，动力关闭时验证上电/复位默认禁用；各模块共地，电机动力与回流使用合适的电源线。
- 电机的具体 12V 规格、堵转电流及双路散热能力仍需补充确认；调试保留物理断电开关。
- 旧 FOC 固件会操作已重新分配的引脚，不可用于 L298N。USB 调试仍隔离降压到 ESP32 的正极线。

## 5. 抛投装置的工作流

新机构的2号电机负责转轴拉绳蓄力，1号电机控制2号位置以实现释放。当前只做2号一次3秒方向测试；限位/回零、释放完整流程及底盘互锁尚未实现。移除 AS5600 后无原角度反馈，旧 90° 位置闭环和自动返回不能直接沿用。

**2026-10-14 实机试验结果：机构未通过。** 用户报告四项问题：电池像是断路（只能直连电源）、3D 打印孔过大导致滑齿且加限位器失败、卷线轴无半月形杆导致齿轮脱落且无法装回、人力满行程下抛射初始角度仍接近水平而过不了城墙。四项均为现场口述，没有仪器数据；详见[失败记录与证据边界](docs/logs/2026-10-14-throw-test/README.md)。在机构几何与轴/轮毂配合重做之前，继续调试固件不能带来可用抛射。

旧 PS2 蓄力 / FOC 轨迹 / 校准流程完整保存于[旧方案归档](docs/archive/2026-10-14-DRV8313-AS5600/README.md)。

## 6. 当前阻塞

| 项目 | 待完成内容 |
|---|---|
| 抛投机构 · 配合与固定（2026-10-14 新增） | 3D 打印配合孔过大导致齿轮滑齿，限位器补救失败；卷线轴无半月形（D 形）切面，齿轮沿轴向滑动脱落后无法装回。需重新设计轴/轮毂配合、轴向固定与齿轮定位；先拆检确认零件是否已磨损 |
| 抛投几何 · 抛射角度（2026-10-14 新增） | 人力满行程下初始抛射角度仍接近水平、过不了城墙。需重做臂长、转轴高度、限位/释放角与绳路；**不依赖**其余三项修复，也不能靠加大电机力矩解决。目标城墙高度与距离本轮未记录 |
| 抛投供电（2026-10-14 新增） | 用户报告电池像是断路，整机只能直连电源；损坏环节（电芯/保护板/接插件）未定位，也未测电压与导通。属现场判断，不是实测参数 |
| N20 规格与动力适配 | 记录两台电机型号、12V 规格与堵转电流，核对电源、L298N 散热与双路负载 |
| 接线验收 | 确认独立 12V 与 5V 逻辑分支、共地、ENA/ENB 跳帽与下拉，排除原 GPIO21 线束异常 |
| 新控制固件 | 实现双 N20 独立调速/方向、默认禁用、停止、断联与底盘 PWM 资源统一分配 |
| 机械流程 | 明确两台电机职责、行程/原点、限位、物理急停与底盘动作互锁 |

旧 AS5600 通信/磁场/FOC 校准阻塞属于已停用方案，见归档。

### 各子系统验证状态

| 子系统 | 状态 |
|---|---|
| 新抛投 · L298N + 双 N20 | 电气侧：2号独立测试固件已编译、烧录，完成6次独立3000ms电气正转，1号未动作。机械侧：2026-10-14 实机试验四项失败，机构未通过，待重做 |
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
| N20 不转或明显发热 | 断开动力，检查使能跳帽、方向电平、实际输出电压与电流 |
| ESP32 重启 | 供电跌落、共地、电流回路、接触不良、电机干扰 |

## 8. 环境与依赖

| 项目 | 值 |
|---|---|
| Arduino | 便携版 IDE 2.3.10 @ `D:\Download\arduino`，内置 arduino-cli 1.5.1、esptool 5.3.1 |
| ⚠️ 坑 | arduino-cli **必须带 `--config-file`**（数据目录在 D 盘），否则找不到 esp32 核心；脚本已自动带上 |
| 核心 / 库 | `esp32:esp32` **3.3.12**、`Simple FOC` **2.4.0**（库名有空格：`arduino-cli lib install "Simple FOC"`） |
| Python | `pyserial` 3.5、`mcp` 2.3.0（用了 v2 的 `MCPServer` API）、`rapidocr-onnxruntime`、`opencv-python` |
| MCP 注册 | `~/.codex/config.toml` → `[mcp_servers.arduino]`，含 `PYTHONUTF8=1`（否则 Windows 中文乱码） |

## 9. 文档索引

> **接底盘请先看 §9.1 与 §9.2 的前两行**；其余为投掷机构、工具链与历史记录。

### 9.1 模块 README（与总入口互相链接）

根 `README.md` 是**总入口**。下列**每个 sketch / 诊断目录各带自己的 `README.md`**，其顶部都有返回总入口的导航块，模块之间也互相链接。

**旧投掷机构（已停用，历史模块）**

新抛投测试入口：[N20Motor2DirectionCheck](firmware/catapult/N20Motor2DirectionCheck/README.md)，只动2号电机，1号和底盘禁用。

| 模块 README | 内容 |
|---|---|
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
| [新抛投接线](docs/抛投装置-L298N双N20接线与GPIO分配.md) | ★ L298N 双 N20、12V 方案、六根 GPIO 分配与跳帽/供电要求 |
| [旧抛投归档](docs/archive/2026-10-14-DRV8313-AS5600/README.md) | ★ 原文、代码、日志、照片与校验清单 |
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
| `docs/logs/2026-10-14-throw-test/` | ★ 实机抛投四项失败记录（电池断路、滑齿、齿轮脱落、抛射角过平）与证据边界；同目录 `user-report.json` 为机器可读的报告原件 |
| `docs/logs/2026-10-14-n20-motor2-check/` | 2号电机单次3秒正转、5 次追加复现、跳帽异常与源码/映像哈希 |
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

2026-10-14 实机试验后，抛投机构的重做优先于继续调固件：

1. **抛投机构重做（最高优先）**：修 3D 打印配合孔（实测孔径与轴径）、给卷线轴做半月形/D 形切面或加轴向固定与紧定螺钉，重做齿轮限位；拆检确认齿轮内孔与轴是否已磨损。
2. **重做抛射几何**：按目标城墙高度与距离确定臂长、转轴高度、限位/释放角与绳路，先用手工满行程验证角度，再交给电机。
3. **抛投供电定位**：断电后用万用表测电池端电压与通断，确认断点在电芯、保护板还是接插件；在定位前不要把"电池坏"当成结论引用。
4. 明确双 N20 的机械职责、12V 电机规格及堵转电流，准备独立 12V 动力分支，并确认底盘独立分支的实际 VM。
5. 按新表接 L298N 六根信号线，验证逻辑电源、GPIO21/22、两路 EN 下拉和默认禁用。
6. 实现双 N20 控制固件，先逐台低占空比空载点动，再验证停止、断联、限位及底盘动作互锁。
7. 继续验证底盘方向键固件的带载速度、持续运行及断联停车；历史联动停转问题仍需结合供电诊断排查。

## 参考

- [SimpleFOC 位置控制](https://docs.simplefoc.com/angle_loop) · [3PWM 驱动](https://docs.simplefoc.com/bldcdriver3pwm) · [I²C 磁编码器](https://docs.simplefoc.com/magnetic_sensor_i2c) · [电压模式](https://docs.simplefoc.com/voltage_torque_mode)
- [AS5600 产品页](https://ams-osram.com/products/sensors/position-sensors/ams-as5600-position-sensor) · [ESP32 GPIO 文档](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/gpio.html)
- [奇果派驱动板资料](https://www.7gp.cn/archives/308) · [Arduino-PS2X 作者源码](https://github.com/madsci1016/Arduino-PS2X)
