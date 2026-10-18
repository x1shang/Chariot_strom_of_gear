# ChariotIntegrated-v3-R1：完整版烧录与USB启动检查

日期：2026-10-18。当前需求为方向键驾驶、右摇杆转向、按住R2双飞轮、点按R1定时N20滑轨对接器；无舵机。本次操作说明见[当前版本](../../整合程序v3-R1对接器-2026-10-18.md)。

## 仓库与接线

已执行git fetch及git pull --ff-only --autostash origin master，从19105db快进至9a11db22ac06d6c2f64c0cb23fb420198a7165ef；本地HEAD和origin/master相同，原有修改保留，没有提交或推送。用户报告所有改接线完成，并明确确认底盘、130、N20动力断开，外部5V到ESP32正极隔离，ESP32仅接USB。没有舵机。

GPIO14共用L298N双EN，25/26/22为TB3 AIN1/AIN2/STBY，27不输出舵机脉冲，21不用；底盘及PS2不变。GitHub的MecanumPS2Flywheel-v2仍是旧六信号接法，未直接烧录该入口。

## 真实发现与修复

首次写入哈希通过，但USB启动发现hw=0、全部输出保持关闭，手柄通信已为ps=1、mode=73。原因是旧整合初始化对零占空比调用ledcReadFreq，并要求回读目标频率；本机Arduino-ESP32 3.3.12在零占空比时刻意返回0，产生误判。此前模拟层没有覆盖这个API行为。

修复为初始化核对attach成功、写0成功、ledcRead回读0；不在启动时为测频而产生非零PWM。运行期请求非零输出时再核对频率，异常仍停机。模拟层与本机核心语义同步，增加零占空比和运行期异常频率回归。首次失败日志在attempt1-zero-frequency/；最终结果以本目录顶层的compile.txt、upload.txt、startup.json和verification.json为准。

## 当前验证状态

- 主机模拟：679项通过，见host-tests.txt；不连接串口、不运行真实电机。
- 修复版实际编译通过：Arduino-ESP32 3.3.12、esp32:esp32:esp32；程序288983字节，全局变量22544字节，见compile.txt。
- COM4识别为ESP32-D0WD-V3 rev3.1；修复版写入成功，esptool对相应写入数据/未变区域校验通过，见upload.txt。
- USB启动检查通过：15条状态均为hw=1且全部电机指令关闭；ps=1、mode=73、analog=1、buttons=0000，观察到ready=1、armed=1。最后STOP后的ready=0/armed=0是锁定后的即时采样，随后连续中立会重新就绪，见startup.txt、startup.json。
- 四份当前源码与实际编译副本逐字节相同；源码、测试、模拟层、核心实现及最终固件的SHA-256保存在verification.json。

## 操作和边界

- 方向键驾驶，右摇杆横轴比例转向；有效中立5帧自动就绪。
- R2从0经1200ms软启动至100%，单次最多10秒；松开撤销输出。
- 停车点按R1：150ms、30%占空比，松开R1仍按定时完成；长按不连发，两次停止间隔至少800ms；无需R2。飞轮若运行，须已预转2500ms。
- R1在车辆行驶或预转未完成时拒绝且不排队；N20动作中要求驾驶会停机锁定。圆圈、STOP、无效帧或150ms无有效帧均撤销电机输出。
- SELECT/L1/叉不控制机构；SERVO ON被拒绝，GPIO27不输出脉冲。

启动检查脚本只发送STOP和STATUS。没有发送FLY、JOG或SERVO等动作命令，没有给电机通动力。状态值只证明程序/外设回读，不是实测电压、电流或转速。130重新插线后的对转方向、N20对接方向、150ms对应的行程和卡滞保护均未完成实物验收。
