# Conbox 工位盒固件

Puya **PY32F003x8** (Cortex-M0+, 64KB Flash / 8KB SRAM) 工位盒。
扫码枪读条码 → 上位机按 SIACP 协议下发 PASS/NG → 继电器直通 + 灯/OLED 反馈。

## 硬件映射

| 资源 | 引脚 | 说明 |
|---|---|---|
| USART1 | PA2 / PA3 | U1 接扫码枪。收 ASCII + CRLF 条码，发 `0x16 0x54` 触发扫描 |
| USART2 | PA0 / PA1 | U2 接上位机，SIACP 帧协议 |
| 继电器 | PA12 | **低电平吸合**。PASS 吸合(直通开)，NG 释放(直通关) |
| 红灯 | PA4 | 上位机 NG / 红灯命令 |
| 绿灯 | PA5 | 上位机 PASS / 绿灯命令 |
| 黄灯 | PA6 | 条码在途等待判定 (WAIT) |
| 本地拦截灯 | PA7 | 防重命中，本地提示 |
| OLED | PB5 RES / PB6 SCL / PB7 SDA | CH1115 88x48，**软件 I2C** |

引脚常量统一取自 [Core/INC/gpio.h](Core/INC/gpio.h)（Mcu Studio 生成的 pin map），不要在各处硬编码。

## 上位机协议 (SIACP)

定义见 [Core/INC/siacp.h](Core/INC/siacp.h)，来源是上位机 `DataFile/SIACP.xml`。

```
[Head] [Len] [Cmd] [Mark...] [CRC16_H] [CRC16_L]
```

- `Head`：`0xAA` 上位机→盒子请求 / `0xAB` 盒子→上位机应答
- `Len`：整帧字节数（含 Head/Cmd/CRC），`Mark` 最长 32 字节
- `Cmd`：工位盒固定 `0xFA`
- `CRC16`：CCITT-FALSE，poly `0x1021`，init `0xFFFF`，逐 nibble，无反射无 xorout，大端存放

常用 Mark：

| Mark | 含义 |
|---|---|
| `01 27 01` | PASS → 绿灯 + 继电器吸合 |
| `01 27 00` | NG → 红灯 + 继电器释放 + 触发重扫 |
| `01 06 31` / `01 11` | 绿灯常亮 / 单次 |
| `01 06 2F` / `01 12` | 黄灯闪烁 / 单次 |
| `01 06 1F` / `01 13` | 红灯闪烁 / 常亮 |
| `02 17` | 读取移动盒条码 → 盒子回 `02 17 + 条码` |

应答固定 `AB 07 FA 01 0A 30 52`（`Siacp_SendAck`）。

## 防重逻辑

见 [Core/SRC/stationbox.c](Core/SRC/stationbox.c)。

- 条码扫码枪回传后进入 `s_pending`（在途），收到相同条码 → 本地拦截 + PA7 闪
- PASS 后条码进封禁表（RAM FIFO 64 条），再次出现 → 拦截
- NG 后释放 `s_pending`，允许重扫
- 在途期间收到不同条码 → 以最新为准
- `s_pending` 超时 `SB_PENDING_HOLD_MS`(30s) 自动清空

封禁表**只在 RAM，掉电即失**。

## 目录结构

```
Core/SRC/      业务与外设初始化 (stationbox.c / siacp.c / main.c)
Core/INC/      对应头文件 + pin map
OLED/          CH1115 软 I2C 驱动 + 字模表 (oledfont.h 占 ~7.7KB RO)
Drivers/       Puya HAL / LL / CMSIS / BSP (上游库，勿改)
MDK-ARM/       Keil 工程；Objects/T1.sct 是 link.bat 的散列文件
T1.pysprj      Puya Mcu Studio 工程
link.bat       离线 armlink 链接脚本
```

## 构建

### Keil MDK

打开 `MDK-ARM/T1.uvprojx`，Debug/Download。工程配置：`-DPY32F003x8 -DUSE_HAL_DRIVER -DUSE_FULL_LL_DRIVER`，`-O1 --c99 --gnu --thumb`，链接用 **microlib**。

### 离线命令行

`build_tmp/` 与 `Objects/*.o` 被 gitignore，clone 下来是空的，需要先编译。

```sh
cd MDK-ARM
mkdir -p build_tmp

armcc --cpu=Cortex-M0+ --thumb --c99 --gnu -O1 -W -c \
  -DPY32F003x8 -DUSE_HAL_DRIVER -DUSE_FULL_LL_DRIVER \
  -I../Core/INC -I../Drivers/CMSIS/Include \
  -I../Drivers/PY32F003_HAL_Driver/Inc \
  -I../Drivers/CMSIS/Device/PY32F003/Include -I../OLED \
  -o build_tmp/main.o ../Core/SRC/main.c

# startup 汇编必须带 -c，否则产出完整镜像而非 object，会污染段布局
armasm --cpu=Cortex-M0+ -c startup_py32f003.s -o build_tmp/startup_py32f003.o
```

链接跑 `../link.bat`：

```
armlink --cpu Cortex-M0+ build_tmp\*.o --library_type=microlib --strict \
  --scatter Objects\T1.sct --summary_stderr --info summarysizes --map \
  --load_addr_map_info --xref --info sizes --info totals --info unused \
  --info veneers --list build_tmp\T1.map -o build_tmp\T1.axf
```

**漏掉 `--library_type=microlib` 体积会算大约 3 倍**（实测同一份代码 16776 vs 48321 字节）。
有效检查是「0 错误 + 0 未解析符号」；这套 armcc 的 `-W` 不真正开启额外警告，
「0 警告」不可靠。

### 体积参考

实测 `Total ROM = 16632 B`，`Total RW = 1808 B`。RO-data 约 7700 B 几乎全是
`oledfont.h` 字模表，要瘦身从这里下手。

## 当前状态

[Core/SRC/main.c](Core/SRC/main.c) 处于 **Echo 测试模式**：`StationBox_Init()` /
`StationBox_Task()` 被注释掉，U1/U2 逐字节回显到 OLED 三行滚动显示，用来诊断扫码枪数据链路。
业务链路已恢复则取消 `main.c` 里 `StationBox_Init()`（初始化段）与 `StationBox_Task()`
（`while(1)` 内）的注释即可。

## License

用户代码未声明许可。`Drivers/` 为 Puya / ST 上游库，各文件内保留其 BSD 3-Clause 声明。
