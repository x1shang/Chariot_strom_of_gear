# v3.5-R1LONG：两个方向不同时长（2026-10-18）

## 需求

R1 每按一次交替方向，且**两个方向的时长不同**：本意是「上升方向 3 秒」。但**哪个方向是上升尚未确定**，因此随机指定：**第一次点按（+1）走 3000ms，第二次点按（-1）保持 1750ms**。确认上升方向后，改一个常量即可对调。

## 改动

| 项 | v3.4-R1ALT | v3.5-R1LONG |
| --- | --- | --- |
| `Cfg::VERSION` | `ChariotIntegrated-v3.4-R1ALT` | **`ChariotIntegrated-v3.5-R1LONG`** |
| 方向 +1 的 R1 脉冲 | 1750ms | **3000ms**（`FEED_PULSE_MS_FORWARD`） |
| 方向 -1 的 R1 脉冲 | 1750ms | 1750ms（`FEED_PULSE_MS_REVERSE`） |
| `FEED_JOG_MAX_MS` | 3000ms | 3000ms（不变，够覆盖长方向） |
| STATUS `N20ms` | 固定显示 1750 | **空闲时显示「下一次 R1 将要走的时长」**（3000 或 1750 交替） |
| 交替语义 | 每成功点按翻转方向 | 不变；被拒按键不翻转，JOG 不参与 |

- 两个常量互不相等，由 `static_assert` 强制（`FEED_PULSE_MS_FORWARD!=FEED_PULSE_MS_REVERSE`），避免哪天误改回相同值而悄悄失去方向差异。
- 新增 `feedPulseMs(int direction)`，R1 边沿按待发方向取对应时长。
- 其余安全门槛（800ms 冷却、300ms 反向保护、停车/中立/使能回读）与 80% 占空比全部不变。

**如何对调**：若发现上升实际是 -1 方向，把 `FEED_PULSE_MS_FORWARD` 与 `FEED_PULSE_MS_REVERSE` 两个数值互换即可（不要改 `FEED_POLARITY`，那会同时改变两个方向的物理朝向）。

## 验证状态

- 主机模拟检查：**827 项通过**（v3.4 为 822，新增 5 项：两方向常量不等、+1 跑满 3000、1750 时仍在中段、-1 在 1750 结束、STATUS 空闲显示 3000）。
- 编译：`COMPILE_EXIT=0`，Sketch 289863 字节 / 22%，静态 22592 字节 / 6%。日志见 `compile.txt`。
- **烧录已完成（重新插上后重试）**：首次尝试报 `could not open port 'COM9': FileNotFoundError`（开发板当时未连接，只剩蓝牙串口），`upload.txt` 保留了那次失败记录；用户接回后重跑 `arduino-cli upload -p COM9`，写入 290016 字节，`Hash of data verified`，`UPLOAD_EXIT=0`。
- 烧录后 STATUS：`fw=ChariotIntegrated-v3.5-R1LONG hw=1 ps=1 armed=1 N20ms=1750 N20pct=80 N20next=-1 N20last=1 feed=0 TB3=0 fault=none`。`N20last=1` 说明复位后已有点按被接受（走了 +1 的 3000ms 方向），`N20next=-1` 表示下一次按 R1 将是 -1 的 1750ms。

## 产物哈希（本轮编译，未烧录）

- `ChariotIntegrated.ino.bin` = `020281f2…`（见 `verification.json`）

## 实测时长的监视尝试（未取到数据）

烧录后启动 `host/monitor_r1_pulses.py COM9 150`，计划每 200ms 轮询 STATUS、只在 `feed` 变化时记录并自动算出脉冲长度。结果**一个事件都没抓到**：监视开始后串口在读操作中抛 `GetOverlappedResult failed (PermissionError 13)`，随后本机串口列表里 CH340/COM9 **整体消失**，`monitor.txt` 未生成。

这已经是同一台机器上**第二次**该端口消失（第一次发生在 v3.5 首次烧录前，导致 `could not open port 'COM9'`）。两次的共同点是都要在 USB 连着 ESP32 的情况下操作；若后续发现它**恰好在 N20 起动时掉线**，那本身就是供电/USB 电流问题的证据，与 VM 端电压未测量这件事相互印证。也可能是线材或插头接触不良，目前无法区分。

脚本已改为**断线自动重连**（端口消失时记录 `PORT LOST` 并继续等待，恢复后重新打开），可在板子接回后重跑，且即使中途掉线也会落盘已抓到的事件。

## 未验证

- 真实 R1 按键的两个方向时长差异（需硬件在环；目前只有 STATUS 的配置值证据）。
- 哪一个是上升方向。
- 3000ms 长方向带载是否顶到机械限位（无电流/限位保护，仍是开环定时）。
- 端口两次消失的原因（线材/接触/供电/掉电复位未区分）。
