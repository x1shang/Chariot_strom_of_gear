# v3.6-HOLDUD：按住 △ 上升、按住 × 下降（2026-10-18）

## 需求与澄清

需求：**按住正三角 △ 向上升方向旋转，按住 × 向下降方向旋转。**

一开始我以为 △ 会和底盘前进冲突（把 △ 当成了 D-pad 上键）。用户澄清并纠正了这一点：DualShock 的**左侧是十字方向键、右侧是 △○×□**，两者不是同一组。核对本仓库按键位定义后确认：

| 键 | 位 | 用途 |
| --- | --- | --- |
| 左十字 上/右/下/左 | bit 4/5/6/7（`PAD_*`） | 底盘驾驶，**不变** |
| 圆圈 ○ | bit 13（`PAD_CIRCLE`） | 全车急停锁定，**不变** |
| **正三角 △** | **bit 12** | **新增：按住 = 上升**（此前固件完全没用这个位） |
| **叉 ×** | **bit 14**（`BTN_CROSS`） | **新增：按住 = 下降**（v3 起一直空闲） |
| R1 | bit 11 | 交替方向的定长点按（保留） |

所以**不存在冲突**，底盘驾驶与机构控制可以同时存在于不同按键上（但同时操作仍会被既有的“驾驶 + 机构”互锁规则锁定）。

## 行为

- **按住 △**：N20 朝上升方向（`FEED_UP_DIRECTION=+1`）以 80% 占空比连续转；**松手立即停**。
- **按住 ×**：朝下降方向（`-1`）转；松手立即停。
- **两个都按**：△ 优先。
- **安全上限**：单次按住超过 `FEED_HOLD_MAX_MS=3000ms` 会自动停并**锁定**，必须松开两键后才能重新按住。串口打印 `N20 hold CAP; release both buttons`。
- **不动 R1 的交替状态**：按住操作不消耗 R1 的待发方向槽位。
- 仍受既有门槛约束：需要停车/底盘中立、800ms 冷却、飞轮爬坡期间拒绝、使能回读失败即停机。
- R1 交替点按（3000ms / 1750ms）与 80% 占空比**保持不变**。

新按住的起始帧要求是**按键边沿**，因此若在冷却期按下会直接被拒，需松开再按——与 R1 一致。

## 代码改动

- `RobotConfig.h`：`VERSION` → `v3.6-HOLDUD`；新增 `FEED_UP_DIRECTION=1`（哪一侧算“上升”）与 `FEED_HOLD_MAX_MS=3000`（按住上限），并加 `static_assert`（方向只能是 ±1，上限不得超过 `FEED_JOG_MAX_MS`）。
- `ChariotIntegrated.ino`：新增 `BTN_TRIANGLE=1u<<12`；新增 `feedHoldRequest` / `feedHoldTimeout`；`stopFeed()` 清 `feedHoldRequest`；`tickRobot()` 增加按住逻辑（松手即停、换键即停、到上限锁定）；STATUS 新增 `N20hold=`（当前按住方向，0 表示无）。
- `tests/test_integrated_firmware.cpp`：把原来“叉已被移除”的断言改成“叉现在是下降键”，并新增 9 项按住回归（△ 上升且走 `FEED_IN1`、× 下降且走 `FEED_IN2`、未到上限仍转、松手立即停、不消耗交替槽位、超上限锁定、锁定期间按住不重触发、松开清除锁定、松开后可再按）。
- `host/jog_integrated_n20.py`：前置校验的版本串改为 `ChariotIntegrated-v3.6-HOLDUD`。

## 验证

- 主机模拟检查：**867 项通过**（v3.5 为 827，新增 40 项含按住逻辑）。
- 编译：`COMPILE_EXIT=0`，Sketch 290151 字节 / 22%，静态 22600 字节 / 6%。
- 烧录 COM9：写入并 `Hash of data verified`，`UPLOAD_EXIT=0`。
- 烧录后 STATUS：`fw=ChariotIntegrated-v3.6-HOLDUD hw=1 ps=1 armed=1 N20ms=3000 N20pct=80 N20next=1 N20last=0 N20hold=0 feed=0 TB3=0 fault=none`。
- 溯源哈希见 `verification.json`（产物 `ChariotIntegrated.ino.bin` = `e31b9d84…`）。

## 用户现场确认（2026-10-18）

**方向正确**：按住 △ 是上升、按住 × 是下降，与 `FEED_UP_DIRECTION=1` 的设定一致。也就是说：

- **上升 = 原始方向 +1**（GPIO25 / `FEED_IN1` 那一路），也就是 R1 交替里被分配 3000ms 的那个方向；
- **下降 = 原始方向 -1**（GPIO26 / `FEED_IN2`），R1 交替里 1750ms 的那个方向；
- `FEED_UP_DIRECTION` 无需改为 -1，`FEED_POLARITY` 也不动。

这条确认同时补上了 v3.5 记录里“哪一个是上升方向未知”的空白。

## 未验证（需实机按键）

- 3000ms 上限是否够一次完整上升行程（若行程更长，加大 `FEED_HOLD_MAX_MS`，但不超过 `FEED_JOG_MAX_MS`）。
- 无电流与限位保护：顶到机械限位后仍会继续给 80% 直到松手或到上限，属开环定时。
- TB3 的 VM 端电压与电机电流仍未测量。
