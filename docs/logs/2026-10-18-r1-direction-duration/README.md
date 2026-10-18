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
- **烧录未完成**：`arduino-cli upload -p COM9` 报 `could not open port 'COM9': FileNotFoundError`，本机串口列表里 CH340（原 COM9）已消失，只剩蓝牙串口，说明**开发板当前未连接到这台机器**。日志见 `upload.txt`（真实失败记录，未改写）。
- 因此板上仍是 v3.4-R1ALT，本轮的 3000/1750 差异**尚未在硬件上生效**。重新插上开发板后需要重跑烧录并核对 `STATUS fw=ChariotIntegrated-v3.5-R1LONG`。

## 产物哈希（本轮编译，未烧录）

- `ChariotIntegrated.ino.bin` = `020281f2…`（见 `verification.json`）

## 未验证

- 真实 R1 按键的两个方向时长差异（需硬件在环）。
- 哪一个是上升方向。
- 3000ms 长方向带载是否顶到机械限位（无电流/限位保护，仍是开环定时）。
