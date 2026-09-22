# Empty Project Implementation Plan

> 使用 executing-plans，在当前已存在的 worktree 中顺序执行，不使用子代理。

**Goal:** 删除全部现有业务及串口自检，留下可编译的纯初始化工程。

**Architecture:** 应用入口仅保留初始化和空循环。硬件初始化、HAL/CMSIS、启动代码及 OLED 底层驱动保持原样。

**Tech Stack:** C99、PY32 HAL、Keil ARMCC5、PowerShell 验收脚本。

## Global Constraints
- 用户已批准全部业务及 UART1 自检一并删除。
- 不回退现有修改，不修改无关驱动，不自动提交或烧录。
- 保留原有外设配置与 GPIO 初始电平。

## Task 1：建立清理验收
- [x] 添加 tests/empty_project/run.ps1，检查旧文件缺失、入口仅初始化且空循环、工程无残留引用、所有工程源文件存在。
- [x] 在删除前运行，确认检查因旧业务尚在而失败。

## Task 2：清理与集成
- [x] 精简 Core/SRC/main.c，移除业务、自检、诊断函数、回调、变量和条件编译分支。
- [x] 删除 Core 下 stationbox、siacp、uart1_self_test 的三个源码和三个头文件。
- [x] 清理 MDK-ARM/T1.uvprojx 与 T1.uvoptx 对应 File 节点，保留其余设置。
- [x] 删除 tests/uart1_self_test 的三个文件、2026-09-18 自检设计与计划、旧业务/自检 HEX 和相关中间产物。
- [x] README 改为基础工程说明；通过验收脚本。

## Task 3：验证
- [x] Keil -r 全量编译并检查错误、警告及链接结果。
- [x] 核对底层文件哈希、残留引用和变更差异。
- [x] 记录验证结果，明确未烧录或做板上测试。

## 验证记录
- 清理前结构检查：15 项失败，确认能检测旧模块及其调用。
- 清理后结构检查：全部通过；工程源文件均存在。
- Keil ARMCC 5.06 update 7 (build 960) 全量重编译：退出码 0，0 错误、0 警告。
- Program Size：Code=5400，RO-data=352，RW-data=208，ZI-data=792。
- fromelf 检查新 T1.axf：无旧业务、自检或诊断符号。
- 546 个保留底层文件的 SHA256 与操作前一致。
- git diff --check 通过；无硬件烧录、板上验证或 Git 提交。
