# 工位盒条码防重、放行与显示 — 验证记录

日期：2026-09-19
对应计划：`docs/superpowers/plans/2026-09-19-barcode-dedup.md`
对应需求：`docs/superpowers/specs/2026-09-19-barcode-dedup-design.md`（V2 定稿；灯光规则以 V3 修订第 13 节为准）

本文记录**已实测**的主机测试与目标编译证据，以及**尚未执行**的板上验收项。
机上测试与 Keil 编译不能替代实物联机验收；凡标为“未执行”的用例均不得视为通过。

---

## 1. 已执行：主机测试

执行环境：MinGW `G:\MinGW64\mingw32\bin\gcc.exe`，`-std=c99 -Wall -Wextra -Werror -pedantic -g`。
任一套件编译或断言失败即整体失败（非零退出）。

```powershell
powershell -NoProfile -File .\tests\barcode\run.ps1 -Suite all
```

实测输出：

```text
PASS store: exact bytes, duplicate order, 150/151 FIFO, multi-wrap, reset
PASS rx: lengths 0..100, CRLF, exact bytes, cross-state discard, recovery
PASS protocol: four exact frames, all split points, bad frame resync, ACK/rescan
PASS app: state/command matrix, raw upload, FIFO, relay/lights/wrap, auto lights, communication fault
PASS port: chronological RX, half-frame discard, copied TX/BUSY, ring/queue overflow, UART errors, pins, light phases
PASS view: six-row layout, full 21-byte barcode, nonprintable copy, incremental render, superseded view
PASS echo: raw U1 hex window, chronological order, wrap, binary bytes, no idle redraw, refresh not abandoned, total cap
PASS echo_port: selected channel echoed, other channel ignored, label matches port
PASS echo_port: selected channel echoed, other channel ignored, label matches port
PASS oled: actual driver all 48 rows, bounded refresh bus clocks, no IRQ masking
PASS: 10 suite(s)
```

编译产物写入 `MDK-ARM/build_tmp/barcode/`（已被 `.gitignore` 的 `build_tmp/` 排除）。

### 1.1 需求用例 T01–T30 到测试函数的映射

| 用例 | 覆盖位置 |
|---|---|
| T01 | `test_app.c:test_flow`（锁定＋原样上传＋ACK）；`test_port.c:test_order`；上电重扫取消见 T23–T26 |
| T02 | `test_app.c:test_flow`（WAIT 期重复码不增加上传数） |
| T03 | `test_app.c:test_flow`；`test_port.c:test_order`（B 整帧丢弃，C 可再上传） |
| T04 | `test_app.c:test_flow`、`test_matrix`（PASS 入库、计数 1、吸合、绿灯自动点亮；V3 已取代 V2 的"不主动切灯"） |
| T05 | `test_app.c:test_flow`（VIEW_DUPLICATE，不上传不重扫）；`test_store.c`（重复不刷新 FIFO 顺序） |
| T06 | `test_app.c:test_flow`（IDLE＋VIEW_FAILED＋一次 `16 54 0D`） |
| T07 | `test_port.c:test_order`（等待期 B 已丢，红灯仅发一次重扫，不回放 B） |
| T08 | `test_app.c:test_flow`（失败后 B 重新可上传） |
| T09 | `test_app.c:test_flow`（Tick 100000 后仍 WAIT，无重扫、无解锁） |
| T10 | `test_store.c`（150/151 淘汰、第二条保留、被淘汰码可再传）；`test_app.c:test_fifo_reset_fault` |
| T11 | `test_app.c:test_fifo_reset_fault`（Init 后计数 0、IDLE、不放行）；`test_store.c`（Init 清零） |
| T12 | `test_rx.c`（n=21 分批、CRLF 跨调用边界） |
| T13 | `test_rx.c`（n=0、n=22..100 整条丢弃）；`test_app.c:test_boot`（空码不取消上电重扫） |
| T14 | `test_store.c`（完整字节比较、前缀不等、含空格）；`test_rx.c`（` A b` 原样保留） |
| T15 | `test_app.c:test_lights_time`（500/500 相位、绿灯覆盖闪烁、红灯转闪烁）；`test_port.c:test_commands`（红闪与黄闪同为 500/500，经真实端口与 HAL 桩）；`hal_stub.c` 对三灯互斥加断言 |
| T16 | `test_app.c:test_matrix`（state=IDLE × 四命令均 ACK，PASS 不建记录不放行且灯不变；红灯命令点亮红闪但不重扫） |
| T17 | `test_protocol.c`（四帧精确匹配、全部拆分点、噪声前缀、坏帧后重同步） |
| T18 | `test_app.c:test_flow`（失败后 `s.count` 仍为 1） |
| T19 | `test_app.c:test_flow`（RELEASING 期扫码不上传）；`test_port.c:test_release_history` |
| T20 | `test_app.c:test_flow`（999/1000 ms 边界）；`test_port.c:test_release_history` |
| T21 | `test_app.c:test_flow`、`test_matrix`（放行期重复 PASS 仅 ACK，不重复入库、不续时） |
| T22 | `test_app.c:test_flow`（放行期红灯仅改灯，继电器 1010 ms 才释放）；`test_port.c:test_release_history`（红灯事件晚于截止点时不重扫） |
| T23 | `test_app.c:test_boot`（t0+300/400/500 各一次，9000 ms 后仍为 3）；`test_port.c:test_boot_order` |
| T24 | `test_app.c:test_boot`（第 1 次后接受，取消第 2、3 次） |
| T25 | `test_app.c:test_boot`（第 2 次后接受，取消第 3 次） |
| T26 | `test_app.c:test_boot`（300 ms 前接受，三次全部取消） |
| T27 | `test_app.c:test_boot`（取消后失败仅一次重扫，Tick 9000 不恢复）；`test_port.c:test_cancel_queued_boot`（已入队正常失败重扫不被 `CancelStartupRescans` 误删） |
| T28 | `test_app.c:test_boot`（空条码不取消）；`test_port.c:test_boot_order`（`\r\n` 到点后首帧仍发出） |
| T29 | `test_view.c`（21 字节分行、存储与上传字节未被显示修改） |
| T30 | `test_view.c`（失败提示期间接受新码，显示随新业务更新，增量绘制不阻塞） |

### 1.2 计划列出的额外工程回归

| 回归项 | 覆盖位置 |
|---|---|
| 跨状态半帧 | `test_rx.c`（忙时开始/结束、整帧丢弃）；`test_port.c:test_release_history`（放行 999 ms 起扫、1019 ms 的 `part` 与后续 `tail` 全丢，下一轮需新扫码） |
| 积压事件顺序 | `test_port.c:test_order`（A→B→红灯→C 按实际到达顺序处理，红灯只发一次重扫） |
| uint32 时间回绕 | `test_app.c:test_lights_time`（PASS 于 `UINT32_MAX-499`，500 ms 后释放）；`test_boot`（`t0 = UINT32_MAX-99`）；`test_app.c:test_lights_time` 的历史事件 15 ms 早于 20 ms 吸合起点，不得无符号下溢提前释放 |
| 主机噪声重同步 | `test_protocol.c`（末字节翻转后完整重同步到有效帧） |
| RX 溢出 | `test_port.c:test_rx_fault`（129 字节压满 128 槽环，溢出标记故障，残片不拼接成条码） |
| UART 错误 | `test_port.c:test_rx_fault`（PE/NE/FE/ORE 全遍历；HAL 在 ErrorCallback 前带 ErrorCode 送达的字节不接收；RX 重挂失败重试） |
| TX_BUSY / 失败 | `test_port.c:test_tx`（16 槽填满后拒绝并标记故障、HAL_BUSY 留在队首重试、HAL_ERROR 计数）；`test_cancel_queued_boot` |
| TX 完成槽位可复用 | `test_port.c:test_completed_slot`（填满 16 槽，完成回调后该槽立即可用，不报假溢出） |
| OLED 分片延迟 | `test_oled.c`（282 步 × 2 字节遍历全部 48 行，单步总线时钟数有界，全程不关中断） |
| 单轮绘制上限 | `test_view.c`（每轮最多一个字符、快照未变时不刷新） |
| 刷新不被中途打断 | `test_view.c:test_refresh_not_abandoned`（刷新进行到 40/282 时快照变化，断言刷新期内不得重新绘制；已用还原缺陷版本反向验证该断言会失败） |
| 通信故障不掩盖灯光与应答 | `test_port.c:test_fault_masks_pass`（一次扫码枪帧错误后：PASS 不吸合继电器、条码停止上传，但灯光仍响应、ACK 仍发出） |
| 故障时已入队主机帧的处置 | `test_port.c:test_fault_masks_pass` 末段（错误发生前已入队的 PASS 帧随事件环一并丢弃，不回 ACK） |
| 灯光由业务状态自动驱动 | `test_app.c:test_auto_lights`（接受条码即黄闪；PASS 亮绿灯、继电器释放即全灭；红灯命令转红闪；重复提示全灭）。已用"屏蔽自动灯路径"的反向版本验证该测试确实失败 |
| 手动灯光命令的覆盖边界 | `test_app.c:test_auto_lights` 末段（绿灯覆盖可跨时间保持、可被红灯命令替换；接受条码与 PASS 生效即交回自动灯效） |
| 红闪与黄闪相位一致 | `test_port.c:test_commands`（红灯命令经真实端口与 HAL 桩后按 500/500 闪烁，与黄灯命令同节奏） |
| U1 回显窗口的字节顺序 | `test_echo.c:test_hex_order`、`test_wrap`（最左为最早、最右为最新；满 16 字节后旧字节从左侧滚出） |
| 回显不做 ASCII 替换 | `test_echo.c:test_binary_bytes`（`00 FF 0D 0A` 按原值显示，可与上位机发出的十六进制逐字节比对） |
| 回显刷新不中断 | `test_echo.c:test_refresh_not_abandoned`（刷新进行到 40/282 时收到新字节，先完成既有刷新再重绘）；`test_no_idle_redraw`（无新字节不重绘） |
| 回显只取所选串口 | `test_echo_port.c`（`echo_u1`／`echo_u2` 两次编译，`BARCODE_ECHO_PORT=0`／`=1`；两路各发 4 字节，仅所选路进入画面，`BYTES` 计数证明另一路未被回显）。已用把通道判断改为 `!=` 的反向版本验证该测试会失败 |
| 回显不进入生产固件 | `run.ps1` 仅 `echo`、`echo_u1`、`echo_u2` 三个套件带 `-DBARCODE_ECHO_MODE`；`check_project.ps1` 未定义该宏，其余套件与生产构建均不含回显代码 |

## 2. 已执行：工程结构检查

```powershell
powershell -NoProfile -File .\tests\barcode\check_project.ps1
powershell -NoProfile -File .\tests\empty_project\run.ps1
```

实测结论：两者均通过，`check_project.ps1` 共 **96 项**检查（按输出的 `PASS:` 行数核对，非脚本自报）。关键项：

- 六个新源文件在工程内只引用一次；两路 USART IRQ 转发保留。
- `HAL_UART_RxCpltCallback`、`HAL_UART_TxCpltCallback`、`HAL_UART_ErrorCallback` 各只定义一次。
- `stationbox.*`、`siacp.*`、`uart1_self_test.*` 不再存在。
- 应用层无阻塞操作、无动态内存分配；防重不写 Flash。
- UART 速率仍为 9600。

`tests/empty_project/run.ps1` 已改为显式迁移入口，旧的“main 必须为空循环”“不得调用 OLED/Receive_IT”冲突断言已移除。

## 3. 已执行：Keil 全量重建

```powershell
$project = Join-Path (Get-Location).Path 'MDK-ARM\T1.uvprojx'
$log     = Join-Path (Get-Location).Path 'MDK-ARM\build_tmp\barcode\keil-rebuild.log'
$p = Start-Process -FilePath 'G:\keil5\UV4\UV4.exe' -ArgumentList @('-r', ('"' + $project + '"'), '-t', 'T1', '-o', ('"' + $log + '"')) -NoNewWindow -Wait -PassThru
Get-Content -LiteralPath $log
```

**本机坑：必须用 `-NoNewWindow`。** 用 `-WindowStyle Hidden` 时 UV4 会静默退出、退出码仍为 0、不写日志也不更新 `Objects/T1.axf`，极易误判为构建成功；须核对日志内容与 `.axf` 时间戳。此前 UV4 进程残留还会吞掉后续命令行构建（Keil 单实例转发），重建前先确认没有 `Uv4` 进程。

实测日志（`MDK-ARM/build_tmp/barcode/k7.log`，`T1.axf` 时间戳同步更新，进程退出码 0）：

```text
linking...
Program Size: Code=11730 RO-data=7818 RW-data=252 ZI-data=6420
".\Objects\T1.axf" - 0 Error(s), 0 Warning(s).
Build Time Elapsed:  00:00:02
```

编译器 `V5.06 update 7 (build 960)`，目录 `G:\keil5\ARM\5.06_SLENK\Bin`。日志中 `barcode_app.c`、`barcode_port.c`、`barcode_view.c` 等六个新模块均出现在编译列表中。Keil 命令行构建未改写 `T1.uvprojx`、`T1.uvoptx`、`T1.uvguix.slenk_`（本次重建前后时间戳均未变化）。

**当前工程未启用回显模式，`Objects/T1.axf` 是生产固件。** `MDK-ARM/T1.uvprojx` 的 `C/C++ → Define` 已回到 `PY32F003x8,USE_HAL_DRIVER,USE_FULL_LL_DRIVER`。经镜像字符串与链接器符号表核对（`MDK-ARM/Listings/T1.map`、`Objects/T1.axf`）：`BarcodeView_Format`（236 B，`0x080009f9`）与 `BarcodeView_Poll`（140 B，`0x08000b29`）在镜像内，业务字符串 `COUNT:000/150`／`BARCODE:`／`COMM ERROR`／`RECOGNIZING` 均在；回显符号 `BarcodeEcho_Format`／`BarcodeEcho_OnByte` 不在符号表中，`barcode_port.o(i.BarcodePort_Poll)` 对其引用计数为 0，镜像内无 `RX ECHO`／`BYTES:`／`0123456789ABCDEF` 字符串。

回显构建实测记录（均已从工程中关闭，仅留作对比）：U1（`MDK-ARM/build_tmp/barcode/k8-echo.log`）`Code=11734 RO-data=7834 RW-data=256 ZI-data=6400`；U2（`k9-echo-u2.log`）`Code=11742`，`RO-data`／`RW-data`／`ZI-data` 与 U1 相同（改端口只多 8 B Code，为 `barcode_port.c` 里一次通道比较，标签串等长）。两者均 `0 Error(s), 0 Warning(s)`。回显占用为 `hex_digits` 17 B（`0x08002ec3`，constdata）与 `echo_bytes` 16 B（`0x200014b6`，bss，紧邻栈底 `0x20001810`，间隔 842 B）。

要重新开启回显，在 Define 中追加 `BARCODE_ECHO_MODE`（回显 U1）或 `BARCODE_ECHO_MODE,BARCODE_ECHO_PORT=1`（回显 U2）后重建。

六个新模块 `barcode_store.c`、`barcode_rx.c`、`host_protocol.c`、`barcode_app.c`、`barcode_port.c`、`barcode_view.c` 均已参与编译，无重复 HAL 回调、无重复 `main`。

### 3.1 资源占用与实物容量核对

| 项 | 数值 |
|---|---|
| Code | 11730 B |
| RO-data | 7818 B |
| RW-data | 252 B |
| ZI-data | 6420 B |
| **Flash 合计（Code+RO）** | **19548 B ≈ 19.09 KiB** |
| **RAM 合计（RW+ZI）** | **6672 B ≈ 6.52 KiB** |
| 栈 | 512 B，`0x20001810`–`0x20001A10`（`__initial_sp = 0x20001A10`） |
| 堆 | 256 B，被链接器移除（未使用，无动态分配） |

**工程配置与实物容量不一致，需知悉：**

- Keil 工程 XML（`MDK-ARM/T1.uvprojx`）声明 `IRAM(0x20000000, 0x4000)` = **16 KiB**、`IROM(0x08000000, 0x20000)` = **128 KiB**。
- 同库的 GCC 链接脚本 `Drivers/CMSIS/Device/PY32F003/Source/gcc/py32f003x8.ld` 声明 `RAM ... LENGTH = 8K`、`FLASH ... LENGTH = 64K`。

按链接脚本，PY32F003x8 实物为 64 KiB Flash / 8 KiB SRAM。据此：

- Flash 余量：`65536 - 19548 = 45988 B`（70.2% 余量）。
- RAM 余量：`8192 - 6672 = 1520 B`（18.6% 余量）。

因此本固件按实物容量是装得下的，但**余量以 8 KiB SRAM 为口径**，不是 Keil 声明的 16 KiB。Keil 的内存区声明大于实物，意味着未来一次“Keil 链接通过”不能证明固件能装进芯片。按计划约束未擅自改动内存区，此处仅记录差异，须由硬件负责人确认实际订货型号。

栈仅 512 B，属偏紧项：当前主循环无递归、最大栈内缓冲为 23 字节上传帧，机上看不到问题，但**须在板上用调试器实测最大栈深度**（见 §4）。

## 4. 未执行：板上验收

以下项目**尚未执行**，不得视为通过。主机测试与编译无法覆盖真实波特率字节、GPIO 电平、继电器吸合时间与物理时序。

| 编号 | 验收项 | 期望 | 状态 |
|---|---|---|---|
| B01 | 逻辑分析仪核对 USART1/USART2 两路 9600 8N1 原始字节 | 上传帧为条码原字节＋`0D 0A`，ACK 为 `AB 07 FA 01 0A 30 52` | 未执行 |
| B02 | 四条上位机命令各自触发一次统一 ACK | 仅完整帧产生 ACK，业务效果随当前状态而定 | 未执行 |
| B03 | `t0` 后 300／400／500 ms 各发一次 `16 54 0D`，之后停止 | 三次、间隔 100 ms、不循环 | 未执行 |
| B04 | 接受有效条码后取消剩余上电重扫 | 失败后只补一次正常重扫，不恢复剩余次数 | 未执行 |
| B05 | PA12 低电平持续 1000 ms 放行脉冲 | PASS 后吸合，1000 ms 自动释放；继电器吸合/释放与电平对应关系需现场核对 | 未执行 |
| B06 | 放行期间重复 PASS 与红灯 | PASS 不续时不重复入库；红灯仅改灯，不撤销放行、不重扫 | 未执行 |
| B07 | 黄闪与红闪均为 500 ms 亮／500 ms 灭，三灯互斥 | 绿灯覆盖时停止闪烁，任意时刻至多一灯亮 | 未执行 |
| B08 | OLED 全 21 字符条码分行显示 | 存储与上传字节保持完整，显示仅替换不可打印字节 | 未执行 |
| B09 | 连续收发不丢字节 | RX 事件环不溢出、TX 队列不丢弃，故障时可见而非静默丢帧 | 未执行 |
| B10 | 150／151 FIFO 与重启清零 | 第 151 条淘汰首条、计数保持 150；断电重启后计数归 0 | 未执行 |
| B11 | 关键服务间隔实测 | `BarcodePort_Poll`＋`BarcodeView_Poll` 单轮不超过 1 ms 工程目标；超标时缩小 OLED 片段，不得延长放行设定值 | 未执行 |
| B12 | 最大栈深度 | 调试器实测栈顶不低于 `0x20001810`；若逼近须增大 `STACK` | 未执行 |
| B13 | OLED 全 6 行完整显示、状态切换后无残留旧行 | 上电 IDLE 与扫码 RECOGNIZING 各显示 6 行，无半屏旧内容 | 未执行 |
| B14 | 通信故障可观测性 | 人为制造扫码枪帧错误后，OLED 第 6 行显示 `COMM ERROR`，重启前业务不恢复（PASS 不吸合、条码不上传），灯光与 ACK 仍有效 | 未执行 |
| B15 | 自动灯效与业务状态对应 | 接受条码后黄灯 500/500 闪；PASS 后绿灯亮且 PA12 同为 1000 ms；继电器释放瞬间绿灯灭、三灯全灭；空闲扫码命中已成功记录时全灭 | 未执行 |
| B16 | 手动灯光命令的覆盖与交回 | 空闲发绿/红灯命令立即点亮（红灯为 500/500 闪）并可跨时间保持；再接受条码即交回自动灯效（黄闪）；PASS 生效交回绿灯 | 未执行 |

`t0` 可用调试器时间标记取得，未占用任何新 GPIO。

## 5. 能力边界（非缺陷，须随固件一并交付说明）

- 结果报文无条码或事务编号，工位盒只能把结果应用到当时唯一的待识别条码；空闲时收到迟到结果不会建记录、不放行、不重扫。
- 若旧结果跨轮次迟到，现有报文无法可靠区分新旧结果；须由上位机保证结果不跨轮次，或另行引入事务关联机制。
- PASS 是识别成功依据；**绿灯常亮不是成功依据**。
- V3 起灯光由业务状态自动驱动；三条主机灯光命令保留为强制覆盖，持续到下一个业务事件（接受条码、PASS 生效、继电器释放、重复提示）才交回自动灯效。空闲状态收到红灯命令会点亮红闪且不重扫，该闪灯会一直保持到下一次接受条码，属设计行为而非异常。
- 绿灯总时长等于继电器放行时长 1000 ms；现场最初口头为 3000 ms，确认后按"继电器释放即灭"执行。
- 无识别超时解锁：未收到 PASS 或红灯结果时，无论等待多久都保持锁定。
- 防重仅覆盖单个工位盒、当前上电周期、尚未被 FIFO 淘汰的成功记录；**不跨重启、不跨工位盒、不是永久唯一**。FIFO 显示 150 表示当前有效缓存条数，不是累计产量。
- 传输队列只是字节/输出帧缓冲，不代表忙时扫码排队等待识别；等待与放行期间收到的扫码整帧丢弃。
- 队列或硬件故障会置通信故障并停止接受新任务；已开始的正常放行脉冲仍按原截止点释放，不自动重扫、不清库。故障是可观察状态，不是静默丢帧。

## 6. 执行摘要

- 主机测试：10 个套件全部通过，覆盖 T01–T30 全部需求用例与计划列出的额外回归。
- 工程结构检查：96 项通过；两个检查入口均通过。
- 目标编译：ARMCC 5.06 全量重建四次（生产 `k7.log`、回显 U1 `k8-echo.log`、回显 U2 `k9-echo-u2.log`、关掉回显后复建 `k10-prod.log`），均 0 Error、0 Warning。当前 `.axf` 为生产固件，按实物 64K/8K 容量 Flash 占 29.8%、RAM 占 81.4%；回显固件多约 20～28 B Flash、少 16 B RAM。
- 调试模式：新增 `BARCODE_ECHO_MODE`（开启回显）与 `BARCODE_ECHO_PORT`（`0`=U1 上位机，`1`=U2 扫码枪，默认 0）两个宏，OLED 回显所选串口的原始字节（16 字节十六进制窗口＋累计字节数），用于联调串口。代码在 `barcode_view.c` 内、未新增源文件；`BARCODE_ECHO_MODE` 在源码中无默认定义，只由编译器 `-D` 给出。两个宏现已从 `MDK-ARM/T1.uvprojx` 的 Define 中移除，当前 `.axf` 为生产固件。回显本身无硬件验收项，属联调工具而非业务行为。
- 现场调试后补修与变更：`BarcodeView_Poll` 刷新被中途打断导致面板残留半屏旧内容（已修并加回归测试）；`comm_fault` 一旦锁存即永久停止业务但灯光与 ACK 照常，需现场按 B08/B14 确认；灯光按 V3 改为由业务状态自动驱动（黄闪／绿灯／红闪／全灭），手动灯光命令保留为强制覆盖，见需求第 13 节。
- 板上验收：B01–B16 全部**未执行**。
- 已知待确认：Keil 工程内存区声明（16K/128K）大于链接脚本声明的实物容量（8K/64K），未擅自修改。

---

## 7. 追加：2026-09-22 V4 修订复测

范围：新增 `<1>` 版本查询（当前版本 101）与 `<11>` `0xFF` 全灯闪烁，需求见 `docs/superpowers/specs/2026-09-19-barcode-dedup-design.md` 第 14 节。本文第 1–6 节为 2026-09-19 的记录，保持原样不改写。

### 7.1 主机测试（复跑全部 10 套件，全部通过）

```text
PASS store: exact bytes, duplicate order, 150/151 FIFO, multi-wrap, reset
PASS rx: lengths 0..100, CRLF, exact bytes, cross-state discard, recovery
PASS protocol: CRC vectors, field mapping, splits, corruption rejection, resync, ACK1/ACK2 builder, lamp table
PASS app: state/command matrix, raw upload, FIFO, relay/lights/wrap, auto lights, communication fault, SN query, version query, all-blink
PASS port: chronological RX, half-frame discard, copied TX/BUSY, ring/queue overflow, UART errors, pins, light phases
PASS view: six-row layout, full 21-byte barcode, nonprintable copy, incremental render, superseded view
PASS echo: raw U1 hex window, chronological order, wrap, binary bytes, no idle redraw, refresh not abandoned, total cap
PASS echo_port: selected channel echoed, other channel ignored, label matches port
PASS echo_port: selected channel echoed, other channel ignored, label matches port
PASS oled: actual driver all 48 rows, bounded refresh bus clocks, no IRQ masking
PASS: 10 suite(s)
```

新增覆盖：

| 项 | 覆盖位置 |
|---|---|
| `<1>` 版本应答字节精确匹配（`AB 08 FA 01 FF 65 D1 9D`，CRC `0xD19D`） | `test_protocol.c`（`HostAck_BuildPayload` 构造器独立核对）、`test_app.c:test_version` |
| `<1>` 在空闲／等待识别／放行三种业务状态均回两帧，且不触碰灯、继电器、上传流 | `test_app.c:test_version` |
| `<1>` 不是业务事件：红闪强制覆盖跨版本查询保持 | `test_app.c:test_version` 末段 |
| `<1>` 地址不符时静默丢弃、不发应答 | `test_app.c:test_version` 末段 |
| `0xFF` 三灯同相 500/500 闪、业务事件交回自动灯效、`00` 全灭 | `test_app.c:test_all_blink` |
| 三灯互斥断言放宽为「至多 1 盏，或 3 盏同亮」 | `tests/barcode/fake_port.c`（`BarcodePort_SetLights` 内的不变量，`n<=1 \|\| n==3`） |

### 7.2 工程结构检查

`check_project.ps1` 通过，仍为 **96 项**（按输出的 `PASS:` 行数核对）。本次未新增结构检查项。

### 7.3 Keil 全量重建

`MDK-ARM/build_tmp/barcode/keil-rebuild.log`，UV4 进程退出码 0，`Objects/T1.axf` 时间戳 2026/09/22 16:50:38、415148 B：

```text
linking...
Program Size: Code=12552 RO-data=7788 RW-data=252 ZI-data=6516
".\Objects\T1.axf" - 0 Error(s), 0 Warning(s).
Build Time Elapsed:  00:00:03
```

编译器 `V5.06 update 7 (build 960)`，`G:\keil5\ARM\5.06_SLENK\Bin`。`barcode_app.c`、`host_protocol.c` 均出现在编译列表中。

**基线漂移（须知悉）。** 同一工程、同一编译器、同为 UV4 `-r` 全量重建，但两次结果不同：

| 日志 | 时间 | Code | RO-data | RW-data | ZI-data |
|---|---|---|---|---|---|
| `build_tmp/barcode/k10-prod.log` | 09-19 13:25 | 11730 | 7818 | 252 | 6420 |
| `build_tmp/armcheck/rebuild.log` | 09-19 14:59 | 12414 | 7786 | 252 | 6516 |
| `build_tmp/barcode/keil-rebuild.log`（本次） | 09-22 16:50 | 12552 | 7788 | 252 | 6516 |

第 3 节与 §3.1 记录的是 `k10-prod.log`。`k10` 与 `armcheck` 之间的 +684 B Code／−32 B RO／+96 B ZI 差异产生在本次改动之前，非本次引入；本次改动相对 14:59 基线仅 **+138 B Code、+2 B RO-data**，RW 与 ZI 不变。差异原因未定位（同一工程选项、同一编译器，`-O` 均为 `Optim=1`）。

### 7.4 资源占用

| 项 | 数值 |
|---|---|
| Flash 合计（Code+RO） | 20340 B，占 64 KiB 的 31.0%（余 45196 B） |
| RAM 合计（RW+ZI） | 6768 B，占 8 KiB 的 82.6%（余 1424 B） |

较 §3.1 的 RAM 余量 1520 B 减少 96 B，仍按 8 KiB SRAM 实物口径计算，未改动工程内存区声明。

### 7.5 新增板上验收项

| 编号 | 验收项 | 期望 | 状态 |
|---|---|---|---|
| B17 | `<1>` 版本查询原始字节 | 收到 `AA 07 FA 01 FF 25 B9` 后依次发出 `AB 07 FA 01 0A 30 52` 与 `AB 08 FA 01 FF 65 D1 9D`；各业务状态（含通信故障）均需应答；应答前后灯光、PA12、上传流不变 | 未执行 |
| B18 | `<11>` `0xFF` 全灯闪烁 | 黄／绿／红三灯**同相** 500 ms 亮／500 ms 灭；发绿或红灯命令可切换为单灯；下一次接受条码即交回自动灯效 | 未执行 |

### 7.6 本次未重测项

- 回显固件体积：`Code=11734`（U1）／`11742`（U2）测自 09-19 13:25 基线，本次未重建，仅作量级参考。
- 最大栈深度（B12）：本次改动新增一处 1 字节局部数组与一次 `HostAck_BuildPayload` 调用，未实测。
- B01–B16 状态不变，全部**未执行**。
