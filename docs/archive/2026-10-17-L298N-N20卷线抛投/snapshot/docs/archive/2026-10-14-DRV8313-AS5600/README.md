# 旧抛投方案归档：DRV8313 + 2804 + AS5600

归档日期：2026-10-14。用户报告 DRV8313 已烧坏，决定移除原驱动与 AS5600 磁编码器，改用一块 L298N 驱动两台 N20，采用 12V 动力方案。旧 FOC 方案停止作为当前接线和操作依据。

当前方案：[L298N 双 N20 接线与 GPIO 分配](../../抛投装置-L298N双N20接线与GPIO分配.md) · [仓库总入口](../../../README.md)

## 归档范围与保留方式

`snapshot/` 按原仓库相对路径存放归档前的原始文件，内容逐字节保留。覆盖旧抛投设计、接线、PS2 控制、编码器诊断、烧录与阻塞记录，三个旧固件及模块说明，相关工具、原始串口日志、照片/OCR 与共享接线资料。为保持历史 Markdown 链接可读，相关本地链接目标也一并保存；其中底盘资料属于上下文快照，现行底盘文档仍在原位置维护。

专用旧文档的原路径保留归档导航，原始全文在此目录；共享底盘/全车文档与历史日志保留原路径，避免破坏既有引用。旧固件源码和工具保留在原目录用于追溯，三个模块 README 标明已停用；此目录中所有旧接线、校准、烧录、使能命令均为历史记录，不适用于 L298N。

文件清单、归档来源提交及 SHA-256 校验值见 [manifest.json](manifest.json)。未跟踪的赛事手册 PDF 不属于本归档，保持原样。

`manifest.json` 的 SHA-256 是对**归档当时工作区的实际字节**计算的，其中 45 个文本文件仍是 CRLF。为保证克隆后仍能逐字节复验，`.gitattributes` 对 `docs/archive/**` 显式设为 `-text`（不做任何行尾转换），因此这些快照在索引、检出与工作区三处字节一致。校验方法：对 `snapshot_path` 逐个算 SHA-256，与 `manifest.json` 的 `sha256` 比较（2026-10-14 复核：126 个文件，缺失 0、不一致 0）。

## 主要文档

- [README.md](snapshot/README.md)
- [docs/ESP32-2804-FOC-guide.md](snapshot/docs/ESP32-2804-FOC-guide.md)
- [docs/投掷机构-设计与安全.md](snapshot/docs/投掷机构-设计与安全.md)
- [docs/PS2遥控实现与调试.md](snapshot/docs/PS2遥控实现与调试.md)
- [docs/诊断记录-第一阶段.md](snapshot/docs/诊断记录-第一阶段.md)
- [docs/线路检查记录.md](snapshot/docs/线路检查记录.md)
- [docs/烧录测试记录.md](snapshot/docs/烧录测试记录.md)
- [docs/阻塞问题与烧录实测.md](snapshot/docs/阻塞问题与烧录实测.md)
- [docs/环境检查报告.md](snapshot/docs/环境检查报告.md)
- [docs/全车连接检验-GPIO与降压端子索引.md](snapshot/docs/全车连接检验-GPIO与降压端子索引.md)

## 旧固件模块

- [PS2Catapult](snapshot/firmware/catapult/PS2Catapult/README.md)
- [Stage1_AS5600Test](snapshot/firmware/catapult/Stage1_AS5600Test/README.md)
- [Stage2_FOCClosedLoop](snapshot/firmware/catapult/Stage2_FOCClosedLoop/README.md)

本次共保存 126 个原始文件；新方案尚未编写控制固件或进行硬件测试。
