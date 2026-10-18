# v3.4-R1ALT：R1 交替方向 1.75 秒（2026-10-18）

## 改了什么

| 项 | 旧 | 新 |
| --- | --- | --- |
| `Cfg::VERSION` | `ChariotIntegrated-v3.3-JOG` | `ChariotIntegrated-v3.4-R1ALT` |
| R1 行为 | 固定方向 +1，150ms，30% | **每次成功点按交替方向**，1750ms，**80%** |
| `FEED_PULSE_MS` | 150 | **1750** |
| `FEED_PERCENT` | 30 | **80** |
| STATUS | 无方向信息 | 新增 `N20next=` 与 `N20last=` |

行为定义：

- 按一次 R1 → N20 朝当前待发方向转 **1.75 秒**，到时自动停（不依赖松手）。
- 再按一次 → 朝**反方向**转 1.75 秒；第三次又回到第一个方向。
- **只有被接受的按键才翻转方向**：因冷却、需要停车、飞轮爬坡、手柄不中立等原因被拒的那一次不消耗方向。
- 冷却 800ms、反向保护 300ms、必须停车/中立/使能回读等既有安全门槛全部保留。
- 方向状态跨 `STOP`/解除武装保留，便于连续来回操作；不随 disarm 复位。

为什么用 80%：本机实测 30%/50% 完全起不动 N20，65% 只是抖动，75% 空载能起转，**80% 在带负载下能带动机构 3 秒**（见 `docs/logs/2026-10-18-n20-loaded-3s/`）。旧的 30% 默认值在实物上等于「按 R1 毫无反应」。

## 代码改动

- `firmware/robot/ChariotIntegrated/RobotConfig.h`：版本号、`FEED_PULSE_MS=1750`、`FEED_PERCENT=80`，注释说明交替语义与 80% 的实测依据。
- `firmware/robot/ChariotIntegrated/ChariotIntegrated.ino`：新增全局 `feedNextDirection`；R1 边沿改为 `startFeed(feedNextDirection, FEED_PULSE_MS, false)` 且成功后 `feedNextDirection=-direction`；STATUS 增加 `N20next`/`N20last`；开机提示改为「R1 alternates 1750ms」。
- `tests/test_integrated_firmware.cpp`：新增 `feedActivePin()` 辅助；把写死的 150/160ms 等待改为 `Cfg::FEED_PULSE_MS`；新增交替回归（含「被拒按键不翻转」「第三次回到原方向」「STATUS 暴露待发方向」）；`failFreq=25` 用例固定起始方向。
- `host/jog_integrated_n20.py`：前置校验的固件版本串改为 `ChariotIntegrated-v3.4-R1ALT`。

## 验证

- 主机模拟检查：**822 项通过**（此前 792；新增 30 项覆盖交替与定长脉冲），`no serial hardware or motors accessed`。
- 编译：`COMPILE_EXIT=0`，Sketch 289823 字节 / 22%，静态 22592 字节 / 6%。日志见 `compile.txt`。
- 烧录：写入 289968 字节，`Hash of data verified`，`UPLOAD_EXIT=0`。日志见 `upload.txt`。
- 烧录后 STATUS：`fw=ChariotIntegrated-v3.4-R1ALT ... armed=1 N20ms=1750 N20pct=80 N20next=1 N20last=0 fault=none`。
- 溯源哈希见 `verification.json`（产物 `ChariotIntegrated.ino.bin` = `2e577690…`）。

## 尚未验证（需实机按键）

- **真实 R1 按键**的交替与 1.75 秒时长尚未由用户现场确认；本轮只验证了遥测默认值与模拟回归。
- 方向定义与实际机构是否一致（`FEED_POLARITY=1` 仍是未核对的开环假设）。
- 带负载连续多次交替的温升、堵转、机械到位限位。
- TB3 的 VM 端电压与电流仍未测量。
