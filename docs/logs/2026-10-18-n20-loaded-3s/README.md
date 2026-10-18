# v3.3 固件加点动上限 + N20 80% 带载 3 秒（2026-10-18）

本轮目的：在**带负载**（弹匣/滑轨啮合）的情况下，用 **80% 占空比连续 3 秒**验证 N20 能否真正带动机构。

## 为什么要改固件

原固件 `Cfg::FEED_JOG_MAX_MS=500`，实测 `JOG 1 3000 80` 被拒（`ERR JOG direction/ms/percent/gate`），无法做连续 3 秒。因此本轮改动：

- `firmware/robot/ChariotIntegrated/RobotConfig.h`：`FEED_JOG_MAX_MS` 500 → **3000**，`static_assert` 上限 1000 → 3000。
- `tests/test_integrated_firmware.cpp`：边界用例 `JOG 1 501` → `JOG 1 3001`、`"JOG 1 501 50"` → `"JOG 1 3001 50"`。
- `host/jog_integrated_n20.py`：毫秒上限 500 → 3000；新增 `--loaded-confirmed`，带载测试不必再冒充空载（与 `--unloaded-confirmed` 互斥，带电时必须二选一），结果 JSON 记录 `load_state`。

**注意：`Cfg::VERSION` 仍为 `ChariotIntegrated-v3.3-JOG`，本次未升级版本号。** 因此本板当前固件与仓库里同名 v3.3 的记录**不是同一份二进制**，溯源一律以本文件的源文件哈希与 `verification.json` 为准，不要用版本字符串判断。

## 工具链与环境

本机（用户 `jiahe`）原先没有 ESP32 工具链，本轮补齐：

- `arduino-cli` 1.5.2-rc.1 → `D:\Tools\arduino-cli\arduino-cli.exe`
- ESP32 core `esp32:esp32@3.3.12` 已存在于 data 目录 `D:\Download\arduino\data`
- 注意：`tests/run-integrated-tests.cmd` 写死了 `C:\Users\33920\...\vcvarsall.bat` 与 MSVC `cl`，**本机没有 MSVC**；改用 `g++ -std=c++17 -Itests/integrated-stubs tests/test_integrated_firmware.cpp` 编译，结果一致（792 项全过）。该 cmd 在本机不可直接用。

## 构建与烧录

- 编译：`tools/build-integrated.ps1 -Cli D:\Tools\arduino-cli\arduino-cli.exe -DataDirectory D:\Download\arduino\data`，`COMPILE_EXIT=0`；Sketch 289715 字节（22%），静态变量 22576 字节（6%）。日志见 `compile.txt`。
- 烧录：`arduino-cli upload -p COM9 --fqbn esp32:esp32:esp32 --input-dir <build>`，写入 289856 字节，`Hash of data verified`，`UPLOAD_EXIT=0`。
- 主机模拟检查：**792 项通过**，含放宽点动上限后的 3001ms 边界拒绝回归。

## 带载测试结果

指令 `JOG 1 3000 80`，N20 动力接通、**负载已装上**、用户在场观察：

| 项 | 值 |
| --- | --- |
| 回读 | `feed=1 TB3=1 N20ms=3000 N20pct=80 N20pwm=204`（204/255≈80%） |
| 到时自动撤销 | 是，主机 STOP 之前已 `feed=0 TB3=0 N20pwm=0,0` |
| 故障 | `fault=none`，`error=null` |
| **用户观察** | **转了，机构被带动了** |

记录目录 `powered-80-3000ms-loaded/`（`jog.txt`、`jog.json`）。

## 结论

1. **带负载 80%、连续 3 秒可用**：机构被实际带动，TB3 使能与 A 通道输出在带载下仍正常，`fault=none`。
2. 结合上一轮空载门槛（50% 不动、65% 临界、75% 起转），**80% 兼顾带载裕量**，可作为 R1 对接的工作占空比候选。
3. **`FEED_PERCENT=30` 仍然是个缺陷**：按 R1 实测不会产生任何动作，必须上调后才能上整车。
4. **仍未验证**：TB3 VM 端实际电压与电流、带载下的重复性（只做了一次）、反向带载、连续多次起转的温升、以及电池电压下降后 80% 是否仍够。

## 下一步建议（一次烧录解决）

把 R1 默认参数与版本号一起改，再烧一次：

- `FEED_PERCENT` 30 → **80**
- `FEED_PULSE_MS` 150 → **250**（3 秒是诊断用长脉冲，实际对接不需要这么长）
- `Cfg::VERSION` → `v3.4`，让 STATUS 能区分二进制
- 然后用**真实 R1 按钮**复验，不用 JOG
