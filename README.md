# PY32F003x8 工位盒：条码防重、放行与显示

工位盒位于扫码枪与上位机之间：接收完整条码，与最近 150 条成功记录比对防重后原样上传，由上位机反馈 PASS 或红灯结果，识别成功则输出一次 1000 ms 继电器放行脉冲，并在 OLED 上显示业务状态、条码与成功记录数。

需求定稿见 `docs/superpowers/specs/2026-09-19-barcode-dedup-design.md`（V2 定稿，灯光规则以 V3 修订第 13 节为准，`<1>` 版本查询与 `<11>` 全灯闪烁以 V4 修订第 14 节为准），实施计划见 `docs/superpowers/plans/2026-09-19-barcode-dedup.md`，实测证据与未执行的板上验收项见 `docs/superpowers/verification/2026-09-19-barcode-dedup.md`。

## 行为要点

- **PASS 才是识别成功依据，绿灯常亮不是。** 绿灯命令只改灯。
- **无识别超时解锁。** 未收到 PASS 或红灯结果时，无论等待多久都保持当前条码锁定，不自动释放、不重扫。
- 同一时间只处理一个条码；1000 ms 放行过程也属于本次处理。
- 等待识别与放行期间收到的扫码**整帧丢弃**，不上传、不排队、不替换当前业务。
- 空闲时命中已成功记录则拦截，不上传、不刷新 FIFO 顺序、不主动重扫，仅 OLED 提示重复。
- 识别失败（红灯）解锁当前条码并主动发一次 `16 54 0D` 重扫；失败前已丢弃的扫码不会补传。
- 上电三次重扫在全部硬件与 OLED 初始化完成后 300／400／500 ms 发出；接受任意有效条码即**永久取消**剩余次数，即使该条码随后失败也不恢复。
- 灯由业务状态自动驱动：接受条码等待结果时黄灯闪 500／500；PASS 亮绿灯且继电器释放即灭（绿灯总时长等于放行 1000 ms）；红灯命令亮红闪 500／500，直到接受下一条条码；空闲与重复提示全灭。
- 四条灯光参数（全灭、全灯闪烁、黄／绿／红单灯的熄灭／常亮／闪烁）仍逐字匹配并照发 ACK，但作为**强制覆盖**生效，持续到下一个业务事件（接受条码、PASS 生效、继电器释放、重复提示）再交回自动灯效。
- 三灯互斥对自动灯效与单灯命令成立；`0xFF` 全灯闪烁期间三灯**同相**闪（500／500）是唯一例外，此时三盏同亮。
- `<1>` 查询固件版本（`AA 07 FA 01 FF 25 B9`）照发统一 ACK 后附一字节版本载荷，当前版本 **101**（`AB 08 FA 01 FF 65 D1 9D`）。纯查询：不改业务状态、继电器与灯光，也不结束灯光强制覆盖。版本号在 `HOST_FW_VERSION` 处递增。
- 上电继电器保持释放；仅 PASS 后吸合 1000 ms 自动释放。放行期间重复 PASS 不续时、不重复入库；红灯只改灯，不撤销放行、不重扫、不删除成功记录。

## 能力边界（非缺陷）

- 防重仅覆盖单个工位盒、当前上电周期、尚未被 FIFO 淘汰的成功记录。**不跨重启、不跨工位盒、不是永久唯一**；成功记录只存 RAM，不写 Flash。
- 显示计数 `COUNT:n/150` 是当前有效缓存条数，达到 150 后保持 150，**不是累计产量计数**。
- 结果报文没有条码或事务编号，工位盒只能把结果应用到当时唯一的待识别条码。空闲时收到迟到结果不会建记录、不放行、不重扫；若旧结果跨轮次迟到，现有报文无法可靠区分新旧结果，须由上位机保证结果不跨轮次，或另行引入事务关联机制。
- 传输队列只是字节/输出帧缓冲，不代表忙时扫码排队等待识别。
- 队列或硬件故障会置通信故障并停止接受新任务；已开始的正常放行脉冲仍按原截止点释放，不自动重扫、不清库。故障是可观察状态，不是静默丢帧。

## 上电流程

`HAL_Init()` → `Studio_RCC_Init()` → `Studio_GPIO_Init()` → `Studio_USART2_Init()` → `Studio_USART1_Init()` → `Studio_TIM1_Init()` → `Studio_TIM3_Init()` → `OLED_Init()` → `BarcodeView_Init()` → `BarcodeApp_Init(HAL_GetTick())` → `BarcodePort_Init()` → 主循环。

- `BarcodeApp_Init` 的 `t0` 必须在 `OLED_Init()` 之后取得，因为 OLED 初始化含阻塞延时，会挤占 300 ms 窗口。
- `BarcodePort_Init()` 只做队列初始化与接收启动，不处理业务。
- 主循环只做两件事：`BarcodePort_Poll()`（按事件到达时间推进、处理完事件后再调度当前时刻的定时输出）与 `BarcodeView_Poll()`（快照变化才绘制，单轮最多一个字符/片段）。

## 硬件资源

| 资源 | 引脚 | 配置 |
|---|---|---|
| USART1（上位机） | PA2 / PA3 | 9600、8N1 |
| USART2（扫码枪） | PA0 / PA1 | 9600、8N1 |
| 继电器 | PA12（`KEY_Con_Pin`） | 低有效，上电释放，PASS 后吸合 1000 ms |
| 三色灯 | `LED_R_Pin` / `LED_G_Pin` / `LED_B_Pin` | 高有效，互斥 |
| OLED | PB5 RES / PB6 SCL / PB7 SDA | 88×48，6×8 ASCII 字模 |

串口实际句柄为 `husart1`、`husart2`。两路 UART IRQ 转发保留在 `py32f003_it.c`，三个 HAL UART 回调只定义在 `barcode_port.c`，未新增第二套 IRQ Handler。

## 目录结构

```text
Core/SRC/             外设初始化、中断转发、条码业务六个模块
  barcode_store.c     完整字节 150 条成功记录 FIFO（不写 Flash）
  barcode_rx.c        CRLF 分帧、超长与忙时整帧丢弃
  host_protocol.c     四条主机命令精确匹配、ACK 与重扫常量
  barcode_app.c       单条状态机、灯光、放行与上电重扫调度
  barcode_port.c      HAL 收发、事件时序、GPIO 适配、故障保护
  barcode_view.c      OLED 文本布局与增量绘制
Core/INC/             对应头文件及 HAL 配置
Drivers/              Puya HAL / LL / CMSIS / BSP
OLED/                 OLED 驱动与字模（新增分片刷新接口）
MDK-ARM/              Keil 工程与启动文件
tests/barcode/        主机测试（7 套件）与工程结构检查
tests/empty_project/  迁移入口：旧空工程断言已废止
docs/superpowers/     需求定稿、实施计划、验证记录
```

## 构建

Keil 打开 `MDK-ARM/T1.uvprojx`，目标 `T1`，执行 **Rebuild all target files**，输出 `MDK-ARM/Objects/T1.axf`。命令行全量重建：

```powershell
$project = Join-Path (Get-Location).Path 'MDK-ARM\T1.uvprojx'
$log     = Join-Path (Get-Location).Path 'MDK-ARM\build_tmp\barcode\keil-rebuild.log'
$p = Start-Process -FilePath 'G:\keil5\UV4\UV4.exe' -ArgumentList @('-r', ('"' + $project + '"'), '-t', 'T1', '-o', ('"' + $log + '"')) -NoNewWindow -Wait -PassThru
Get-Content -LiteralPath $log
```

**必须用 `-NoNewWindow`，不能用 `-WindowStyle Hidden`。** 本机上 `-WindowStyle Hidden` 时 UV4 会静默退出、退出码仍为 0、不写日志也不产出 `.axf`；只有用 `-NoNewWindow` 才会真正构建。退出码和进程结束都不能作为构建成功的证据，须核对日志内容与 `Objects/T1.axf` 的时间戳。

最近一次实测（2026-09-22，含 `<1>` 版本查询与 `0xFF` 全灯闪烁）：`Code=12552 RO-data=7788 RW-data=252 ZI-data=6516`，`0 Error(s), 0 Warning(s)`。

**基线漂移须知悉。** 本节早先记录的 `Code=11730 RO-data=7818 RW-data=252 ZI-data=6420` 出自 09-19 13:25 的 `build_tmp/barcode/k10-prod.log`；同一工程、同一编译器（`V5.06 update 7 (build 960)`）在 14:59 的 `build_tmp/armcheck/rebuild.log` 已是 `Code=12414 RO-data=7786 RW-data=252 ZI-data=6516`。本次改动相对 14:59 基线只增 138 B Code、2 B RO-data。回显固件的 `Code=11734`／`11742` 同样测自 13:25 基线，本次未重建。

**内存区声明与实物容量不一致，须知悉。** 工程 XML 声明 IRAM 16 KiB / IROM 128 KiB，而同库 GCC 链接脚本 `Drivers/CMSIS/Device/PY32F003/Source/gcc/py32f003x8.ld` 声明 RAM 8K / FLASH 64K。按实物容量，本固件 Flash 占 31.0%（20340 B）、RAM 占 82.6%（6768 B，余 1424 B）。因此"Keil 链接通过"不能证明固件能装进芯片，实际订货型号需硬件侧确认。工程内存区未为此擅自修改。栈仅 512 B，属偏紧项，需板上用调试器实测最大栈深度。

`link.bat` 仅链接根目录 `build_tmp` 中的对象文件，不负责重新编译；使用前需从当前源码重新生成完整对象集，不能混用旧对象文件。

## 调试模式：OLED 回显串口原始字节

联调用。**`MDK-ARM/T1.uvprojx` 当前未启用**，`Objects/T1.axf` 是交付用生产固件。要打开回显，在 Keil `Options for Target → C/C++ → Define` 里追加 `BARCODE_ECHO_MODE`（再按需追加 `BARCODE_ECHO_PORT=1`）后重建，OLED 即显示所选串口收到的原始字节而不是业务画面：

```text
U2 RX ECHO
BYTES:0008
AA 08 FA 01
06 2F D4 EA
```

- 由两个宏控制，都在 `C/C++ → Define` 里：`BARCODE_ECHO_MODE` 开启回显，`BARCODE_ECHO_PORT=0` 回显 U1（上位机）、`=1` 回显 U2（扫码枪）。不写 `BARCODE_ECHO_PORT` 时默认 0，即 U1。第一行标题随端口显示 `U1 RX ECHO`／`U2 RX ECHO`。
- `BARCODE_ECHO_MODE` 在源码里没有默认定义，只被 `#ifdef` 检测——关掉就是不在 Define 里写它。因此 `-DBARCODE_ECHO_MODE=0` 仍算**开**。
- 第 3～6 行是最近 16 个字节的十六进制，最左为最早到达、最右为最新到达；满 16 字节后旧字节从左侧滚出。
- `BYTES` 是上电以来该串口累计字节数，到 9999 后停在上限，用于确认对端确实发了数据。
- 字节按原值显示（`00 FF 0D 0A` 都可见），不做 ASCII 替换，可与对端发出的十六进制逐字节比对。
- 只改变 OLED 内容：串口收发、命令应答、继电器、三色灯、防重全部照常运行。
- 回显代价：U2 构建 `Code=11742 RO-data=7834 RW-data=256 ZI-data=6400`，比同一基线的生产固件多 28 B Flash、少 16 B RAM。业务的 `BarcodeView_Format` 在回显模式下无人调用，会被链接器裁掉，所以体积差很小。
- **开关核对**：生产固件 `Code=12552 RO-data=7788 RW-data=252 ZI-data=6516`（09-22 全量重建）；回显固件 U1 `Code=11734`、U2 `Code=11742` 测自 09-19 13:25 基线，未随本次重建。关掉后 `strings MDK-ARM/Objects/T1.axf | grep ECHO` 应无输出。`BARCODE_ECHO_PORT` 的所有引用点都在 `#ifdef BARCODE_ECHO_MODE` 内，只删 `BARCODE_ECHO_MODE` 也能正常关掉，但建议两个一起删。

## 测试

```powershell
powershell -NoProfile -File .\tests\barcode\run.ps1 -Suite all   # 10 个主机套件
powershell -NoProfile -File .\tests\barcode\check_project.ps1    # 96 项工程结构检查
powershell -NoProfile -File .\tests\empty_project\run.ps1        # 迁移入口，同样 96 项
```

`-Suite` 可取 `store|rx|protocol|app|port|view|echo|echo_u1|echo_u2|all`。`echo` 验证回显的排版与绘制；`echo_u1`／`echo_u2` 走真实端口层，分别以 `BARCODE_ECHO_PORT=0`／`=1` 编译，验证只回显所选串口、另一路字节不进画面。GCC 编译或断言非零即整体失败。

最近一次实测十个套件全部通过，覆盖需求用例 T01–T30。

**机上测试与编译不能替代硬件验收。** 两路串口原始字节、继电器吸合时间、灯光相位、OLED 实物显示与连续收发不丢字节等均须在板上验证；`docs/superpowers/verification/2026-09-19-barcode-dedup.md` 第 4 节列有 B01–B16 验收单，当前全部标为未执行。

## 已删除的旧业务

原有 `stationbox.*`、`siacp.*`、UART1 串口自检及滚动显示业务已清除，不会由本工程自动恢复。旧业务及串口自检 HEX 已移除，请勿使用旧固件作为当前工程的构建结果。
