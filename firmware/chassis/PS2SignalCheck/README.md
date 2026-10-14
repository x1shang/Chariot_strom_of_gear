# PS2 遥控器信号诊断（所有电机禁用）

日期：2026-10-14。版本：`PS2SignalCheck-v1`。

仅检查 PS2 接收器通信及按键数据，不包含底盘或抛投运动。原 [MecanumPS2Dpad](../MecanumPS2Dpad/README.md) 与 [MecanumPS2](../MecanumPS2/README.md) 源码保留且未经修改；诊断程序沿用其PS2轮询时序。

| PS2信号 | GPIO |
|---|---:|
| CLK | 2 |
| CS / ATT | 4 |
| CMD | 12 |
| DAT | 13 |

底盘STBY=33、L298N ENA=14和ENB=27及所有方向GPIO始终拉低；不附加PWM、不开启电机。默认及故障状态均撤销输出，无法通过命令使能。保留1秒任务看门狗和电机GPIO回读检查。

串口115200，支持`STATUS`、`RAW`与`STOP`。有效帧要求mode为41/73/79且marker=5A；每20ms轮询，150ms无有效更新视为不新鲜。STATUS包括ps、mode、marker、buttons、frames/good/bad计数以及changes。按键转换为高有效，变化时单独打印BUTTONS。

方向键位：上0010、右0020、下0040、左0080；圆圈2000；全部松开为0000。这些位与原底盘固件一致。

有效接收器帧能证明ESP32与PS2接收器之间通信。只有现场按键后数据同步变化，才能进一步证明遥控器当前按键经无线接收器送到主控；持续有效的固定帧不能独自证明无线链路或断联处理正常。

[检查记录](../../../docs/logs/2026-10-14-ps2-signal-check/README.md) · [总入口](../../../README.md)

恢复行驶需重新烧录底盘固件；本诊断不会自动恢复此前N20测试或方向键行驶。
