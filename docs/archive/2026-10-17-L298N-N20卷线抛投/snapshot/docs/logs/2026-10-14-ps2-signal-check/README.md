# 2026-10-14：原遥控驱动保留检查与PS2信号实测

用户要求检查原遥控小车驱动是否还在、遥控器信号是否收到；用户确认遥控器已开机并配对，随后参与现场按键测试。

## 结论

- 原 `firmware/chassis/MecanumPS2Dpad/` 和 `MecanumPS2/` 源码完整保留，git核对与HEAD一致；本次不修改原行驶程序。
- 检查开始时COM9运行 `N20Motor2Ccw10s-v1`，不包含PS2读取，无法据它的STATUS判断手柄信号。
- 已编译并烧录独立 `PS2SignalCheck-v1`，沿用原CLK=2、CS=4、CMD=12、DAT=13的轮询；底盘STBY=33、L298N ENA=14和ENB=27及全部方向输出保持低，不执行任何运动。
- 实机收到mode=73、marker=5A的有效帧；最后一次STATUS为ps=1、hw=1、frames=11544、good=11544、bad=0、changes=36。
- 捕获 `BUTTONS 0000→0002→0000`，以及右方向键 `0000→0020→0000` 的变化，证明遥控器按键变化经接收器传到ESP32；未完整逐一核对四个方向键，不把此测试写成四方向全部验收。
- RAW中还记录模拟摇杆数据从末字节7F变为00，再回7F；原方向键行驶固件不使用摇杆，摇杆移动不能代替十字方向键。
- 最后STOP/STATUS确认STBY=0、ENA=0、ENB=0；串口关闭。未验证车轮运动、无线断联停车或N20与底盘整合控制。

原驱动“仍在”指仓库源码保留，板上程序已因此前N20试验被替换。本次结束板上为只读PS2诊断固件，不能行驶或驱动N20；恢复行驶需重新烧录原底盘固件。

## 软件与烧录

实际固件仿真通过有效/无效模式、按键解码/松键、计数、150ms时效/回绕、运动命令拒绝及所有电机输出保持低。Arduino-ESP32 3.3.12实际编译通过，程序275588字节、静态内存22204字节；应用映像275744字节。COM9上传返回rc=0，esptool写入哈希验证成功。

[诊断源码说明](../../../firmware/chassis/PS2SignalCheck/README.md) · [原方向键驱动](../../../firmware/chassis/MecanumPS2Dpad/README.md) · [原麦轮驱动](../../../firmware/chassis/MecanumPS2/README.md)

证据：[编译](compile.txt) · [烧录](upload.txt) · [上传后状态](idle-after-upload.txt) · [首次采集](capture-01.txt) · [第二次采集与按键变化](capture-02.txt) · [第三次采集与摇杆数据](capture-03.txt) · [最终状态及右方向键](final-status.txt) · [哈希与汇总](verification.json)
