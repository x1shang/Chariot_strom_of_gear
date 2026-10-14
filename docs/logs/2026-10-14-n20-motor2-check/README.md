# 2026-10-14 L298N 2号电机正转3秒检查

用户要求：重新烧录 L298N 程序，2号电机驱动转轴拉绳蓄力，1号电机控制2号电机的位置以实现释放；当前为激发态，只运行2号电机电气正转3秒，不动作1号电机。

源码：[N20Motor2DirectionCheck](../../../firmware/catapult/N20Motor2DirectionCheck/README.md)。主机单次执行工具：[motor2_forward_once.py](../../../host/motor2_forward_once.py)。

## 已完成

- COM9 芯片核对：ESP32-D0WD-V3 rev3.1，Flash 4MB，MAC 08:a6:f7:a8:95:94。
- v2 固件使用 Arduino-ESP32 3.3.12 编译通过：程序275140字节，静态内存22164字节。主机仿真覆盖3秒到期、计时回绕、STOP、重复命令拒绝、默认静止、GPIO回读异常及1号/底盘禁用。
- v2 上传成功，esptool 写入数据哈希校验通过。程序映像275296字节；源码与英文临时目录编译副本 SHA-256 一致，源码/映像哈希见 verification.json。
- 默认上电不动作，只接受一次 FWD3S；1号ENA=14保持低，底盘STBY=33保持低。
- 用户确认动力开启、准备观察后，留5秒准备，仅发送一次 `FWD3S`。运行时 ENB=27 高、IN3=21 高、IN4=22 低；ENA始终低。
- 收到 `DONE FWD3S elapsed=3000 ENB=0`，随后 STOP/STATUS 确认 running=0、used=1、ENA=0、ENB=0、IN3=0、IN4=0、STBY=0。串口关闭，未自动重试、反转、释放或复位机构。

实体转动与实际拉绳/放绳方向等待用户观察确认；以上串口状态和软件计时不等于编码器反馈，也不能证明转轴机械抱死。ENB撤销后为滑行停止。

## 已处理的接线异常

用户最初确认接线和跳帽状态。v1 上传后的静止检查却回读 ENA=1、ENB=1，因此没有发送运动指令。随即增加回读互锁为v2：不符合预期即锁定 hw=0、拒绝运动。

v2 上电时仍读到 ENA=1、ENB=1。用户随后反馈“已拔掉”跳帽；复测两个EN均为0。保持动力关闭，复位清除之前锁定的故障，确认 hw=1、used=0、running=0、两路使能与全部方向脚均为0，才进入动力开启与单次运动阶段。不能把初次高电平时的 STOP 回执当作实际使能已经关闭。

## 证据

- [芯片信息](chip-info.txt)
- [v1 软件检查](software-checks.txt) · [v1 上传](upload.txt) · [v1 静止回读异常](idle-after-upload.txt)
- [v2 软件检查](software-checks-v2.txt) · [v2 上传](upload-v2.txt) · [v2 锁定状态](idle-after-upload-v2.txt)
- [拔跳帽后的复查与复位](idle-after-jumper-removal.txt)
- [一次3秒正转原始记录](motor2-forward-3s.txt)
- [源码/映像哈希与执行摘要](verification.json)

板上现为独立2号电机测试固件；底盘手柄行驶和1号释放控制未包含在此固件中。

## 用户明确要求的两次追加测试

首次测试后，用户两次明确要求“再来一遍”。每次先确认上一轮已停止，再复位清除一次运行标记，确认默认停止，留5秒准备后仅发送一次 FWD3S；没有重新烧录或自动循环。

- 第2次：[复位前后检查](reset-before-repeat-02.txt) · [3秒运动记录](motor2-forward-3s-repeat-02.txt)。DONE elapsed=3000，最终 ENA=0、ENB=0。
- 第3次：[复位前后检查](reset-before-repeat-03.txt) · [3秒运动记录](motor2-forward-3s-repeat-03.txt)。DONE elapsed=3000，最终 ENA=0、ENB=0。

共执行3次独立的3秒电气正转；各次1号释放电机与底盘均保持禁用，串口已关闭。实际机械方向仍待用户反馈。

## 第4次追加测试

用户再次明确要求“再来一遍”。先确认停止再复位，留5秒准备，仅发送一次FWD3S。DONE elapsed=3000，最终ENA=0、ENB=0、IN3=0、IN4=0、STBY=0，串口已关闭。累计4次独立测试，未自动重复。

[复位检查](reset-before-repeat-04.txt) · [第4次3秒记录](motor2-forward-3s-repeat-04.txt)

## 第5次追加测试

用户再次明确要求重复一次。复位前后确认停止，留5秒准备，仅执行一次2号FWD3S，elapsed=3000；1号和底盘保持禁用，结束全部使能为低，串口已关闭。累计5次独立测试。

[复位检查](reset-before-repeat-05.txt) · [第5次3秒记录](motor2-forward-3s-repeat-05.txt)

## 第6次追加测试

用户再次明确要求重复一次。复位前后确认停止，留5秒准备，仅执行一次2号FWD3S，elapsed=3000；1号和底盘保持禁用，结束全部使能为低，串口已关闭。累计6次独立测试。

[复位检查](reset-before-repeat-06.txt) · [第6次3秒记录](motor2-forward-3s-repeat-06.txt)
