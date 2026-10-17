# 2026-10-14：仅2号逆时针运行10秒

用户明确要求：仅2号逆时针运行10秒，1号不动。依据用户此前确认的2号顺时针极性（IN3高、IN4低），本次反向使用IN3低、IN4高。

源码：[N20Motor2Reverse10s](../../../firmware/catapult/N20Motor2Reverse10s/README.md) · 主机：[motor2_counterclockwise_once.py](../../../host/motor2_counterclockwise_once.py)

- 实际固件仿真通过：反向电平、10000ms截止、计时回绕、STOP、重复命令拒绝、GPIO故障互锁、上电静止，1号与底盘始终禁用。
- Arduino-ESP32 3.3.12编译成功，程序275140字节、静态内存22164字节；主机语法检查通过。
- COM9烧录成功，ESP32-D0WD-V3 rev3.1，MAC 08:a6:f7:a8:95:94；应用映像275296字节，各段写入哈希验证成功。
- 上传后确认新版本`N20Motor2Ccw10s-v1`，hw=1、used=0、两路使能及方向输出为0。
- 留5秒准备，仅发送一次CCW10S；运行时ENA=0、ENB=1、IN3=0、IN4=1、STBY=0。
- 收到`DONE CCW10S elapsed=10000 ENB=0`；最后STOP/STATUS回读used=1、running=0、ENA=0、ENB=0、IN3=0、IN4=0、STBY=0。串口关闭，没有重复运动或动作1号。

证据为GPIO回读、输出时限与串口日志，不测量机械角度、速度或停车惯性；机构修复状态不由本次电气测试推断。使能撤销后为滑行停止。

[编译](compile.txt) · [烧录](upload.txt) · [上传后静止](idle-after-upload.txt) · [唯一一次运行记录](counterclockwise-10s.txt) · [源码/映像哈希和执行摘要](verification.json)

板上当前仅支持本次逆时针10秒测试：每次启动最多一次，上电不自动运动；不包含1号、底盘行驶或此前顺时针3秒动作。
