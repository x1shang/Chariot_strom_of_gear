# 2026-10-14 双 N20 顺序测试：2号3秒 → 1号0.5秒

用户请求烧录测试程序：正转为顺时针，先2号蓄力机运行3秒，停止2号，再1号运行0.5秒。用户确认2号在IN3高、IN4低时为顺时针；1号转向尚未确认，本次按IN1高、IN2低做0.5秒方向试验，不能写成已证实顺时针。

源码：[N20SequenceTest](../../../firmware/catapult/N20SequenceTest/README.md) · 主机：[n20_sequence_once.py](../../../host/n20_sequence_once.py)

## 已完成

- COM9 芯片核对为 ESP32-D0WD-V3 rev3.1，Flash 4MB，MAC 08:a6:f7:a8:95:94。
- 实际固件仿真通过：两阶段到期、四种极性、不同时使能、STOP优先于切换、重复RUN拒绝、GPIO回读故障、默认静止与计时回绕。
- Arduino-ESP32 3.3.12 编译通过，程序276180字节、静态内存22188字节；主机脚本语法检查通过。
- 烧录返回rc=0，应用映像276336字节，各写入段均收到 esptool 的哈希校验成功。
- 上传后确认新版本、hw=1、used=0、两路EN与四个IN及底盘STBY均为0。程序上电不自动运动。
- 设置DIR 1 1，留5秒准备，仅发送一次RUN；2号先使能3000ms，关闭2号后1号使能500ms，随后全部关闭。
- 实机串口报告 `TRANSITION motor2_stopped=1 m2ms=3000 motor1_started=1 ms=500`，再报告 `DONE SEQUENCE m2ms=3000 m1ms=500 ENA=0 ENB=0 STBY=0`。
- 最后发送STOP/STATUS，回读used=1、phase=STOPPED、ENA/ENB与四个IN及STBY均为0，串口关闭。未重试或自动重复。

这些是固件输出、GPIO回读和软件计时证据，不代表测得机械速度、角度或惯性停车时刻。电源是否接通、真实机械运动及1号顺时针方向需现场观察；撤销EN为滑行停止，不是保持制动。历史机构失败记录保留，不将本次电气序列完成写成机构修复。

## 证据

- [软件检查](software-checks.txt)
- [编译](compile.txt) · [烧录与写入校验](upload.txt)
- [上传后静止状态](idle-after-upload.txt)
- [唯一一次测试的原始串口记录](sequence-once.txt)
- [源码/映像哈希与执行摘要](verification.json)

板上当前为`N20SequenceTest` v1，仅含这套双 N20 顺序测试，底盘禁用。每次启动最多一次，只有明确DIR后发送RUN才会运动。

## 后续用户观察与当前板上状态

用户确认本次1号实际逆时针，故1号顺时针极性为IN1低、IN2高；2号顺时针极性仍为IN3高、IN4低。首轮原始串口记录保持原样，不将其1号试验写成顺时针。未来顺序测试使用DIR -1 1；主机工具默认d1已改为-1。

用户随后要求1号不动，只让2号再顺时针3秒，已恢复独立2号固件执行一次并停止。板上当前已为`N20Motor2Fwd3s-v2`，详见[第7次独立2号记录](../2026-10-14-n20-motor2-check/README.md)。
