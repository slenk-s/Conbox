# 工位盒条码防重、放行与显示 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking. 仅在用户明确选择代理并行执行后，才改用 superpowers:subagent-driven-development。

**Goal:** 在保留现有底层初始化的基础上，实现单条码识别、150 条成功 FIFO 防重、继电器放行、三色灯、OLED 与上电主动重扫。

**Architecture:** 使用无 HAL 依赖的条码解析、成功 FIFO、命令解析和业务状态机；HAL 适配负责串口队列、GPIO 与时间。中断只收集带时间戳的字节事件和处理发送完成，主循环按接收顺序处理事件并调度定时任务，OLED 最低优先级分片刷新。

**Tech Stack:** C99 子集、现有 PY32 HAL/CMSIS、Keil ARMCC 5、PowerShell、MinGW GCC 主机测试；不引入 RTOS、动态内存或新的第三方库。

## Global Constraints

- 需求依据：`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/docs/superpowers/specs/2026-09-19-barcode-dedup-design.md`（2026-09-19，V2 定稿）。
- 最长 21 字符，结尾 `0D 0A`；先锁定，再完整原样上传；防重不包含结束符，不归一化内容。
- 同一时间只处理一个条码；空闲成功重复码不上传、不重扫；等待与放行期间扫码不上传、不排队。
- 成功记录 150 条，FIFO 淘汰，重复不刷新顺序，重启清空，不写 Flash。
- PASS 才表示识别成功，绿灯不等价 PASS；无识别超时解锁。
- 有效 PASS 立即入库并放行 1000 ms；放行期间 PASS 不续时，红灯不撤销放行、不重扫、不删除记录。
- 等待状态收到红灯，解锁并主动重扫一次；不依赖等待期间收到过什么扫码。
- 黄灯 500 ms 亮／500 ms 灭，三色灯互斥；PASS 不主动改变灯光。
- 上电三次重扫在全部硬件及 OLED 初始化完成后 300、400、500 ms；接受有效新码立即永久取消剩余次数。
- USART1 上位机，USART2 扫码枪；保留 9600 8N1；实际句柄为 `husart1`、`husart2`，不是 `huart1`、`huart2`。
- GPIO 沿用宏：红 `LED_R_Pin`、绿 `LED_G_Pin`、黄 `LED_B_Pin`；灯高有效。继电器 `KEY_Con_Pin` 低有效，上电释放。
- 不恢复已删除的 stationbox、siacp、自检业务；不回退已有无关修改，不自动提交、不烧录。
- 本文件只是实施计划，所有任务尚未执行；30 项需求验收用例并非已通过的测试报告。

---

## 0. 范围、文件分工与执行准备

这是一个共同状态机驱动的固件功能，不拆成互不关联的独立产品。以下模块可独立测试，但按任务顺序集成。

以下所有路径以绝对项目路径展开，执行命令的工作目录固定为：

```powershell
Set-Location -LiteralPath 'G:\PY32\Pro\RS-Con\T1\.claude\worktrees\suspicious-blackburn-5fde4d'
```

### 计划创建的文件（目前均不是已实现接口）

| 绝对路径 | 单一职责 |
|---|---|
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/barcode_store.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_store.c` | 完整字节成功 FIFO |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/barcode_rx.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_rx.c` | CRLF 分帧、超长与忙时整帧丢弃 |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/host_protocol.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/host_protocol.c` | 四种精确主机帧、ACK 与重扫常量 |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/barcode_app.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_app.c` | 单条状态机、灯光、放行与上电调度 |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/barcode_port.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_port.c` | HAL 收发、事件排序与 GPIO 适配 |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/barcode_view.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_view.c` | OLED 文本布局与增量绘制 |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/run.ps1` | 编译并运行主机测试，非零退出即失败 |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_store.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_rx.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_protocol.c` | 底层纯 C 测试 |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_app.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_port.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_view.c` | 业务、HAL 替身及视图测试 |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/fake_port.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/fake_port.h` | 记录上传、ACK、重扫、继电器、灯光动作及模拟时间 |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/check_project.ps1` | 新业务工程引用与基础结构检查 |
| `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/docs/superpowers/verification/2026-09-19-barcode-dedup.md` | 主机测试、编译资源与板上实测证据 |

### 计划修改的现有文件

- `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/main.c`：保留初始化次序，增加 OLED、业务初始化和轮询。
- `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/OLED/oled.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/OLED/oled.h`：只增加单次有限字节刷新接口，保留现有功能；不调用旧滚动接口。
- `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/MDK-ARM/T1.uvprojx`：加入新模块，核对包含目录与链接资源；`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/MDK-ARM/T1.uvoptx` 仅在存在需同步的文件列表时修改。
- `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/empty_project/run.ps1`：明确废止空循环断言，改为调用新的工程结构检查并打印迁移说明。
- `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/README.md`：更新业务说明、构建和测试入口、板上验收要求。
- `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/py32f003_it.c` 已有两路 UART HAL IRQ 转发，原则上无需修改；禁止再添加第二套 IRQ Handler。
- 不修改 HAL、CMSIS、时钟、串口速率、GPIO 模式、既有启动文件。若验证证明必须变更，先说明原因再缩小变更范围。

### 公共数据与接口契约

所有无 HAL 模块头文件使用 `stdint.h`、`stdbool.h`，字节长度用 `uint8_t`，时间用 `uint32_t`。

```c
/* barcode_store.h */
#define BARCODE_MAX_LEN 21u
#define BARCODE_STORE_CAPACITY 150u
typedef struct { uint8_t len; uint8_t data[21]; } Barcode;
typedef struct {
    Barcode entries[150];
    uint16_t head;  /* 最早成功记录 */
    uint16_t count;
} BarcodeStore;
void BarcodeStore_Init(BarcodeStore *s);
bool BarcodeStore_Contains(const BarcodeStore *s, const Barcode *b);
void BarcodeStore_Add(BarcodeStore *s, const Barcode *b); /* 重复不更新顺序 */

/* barcode_rx.h；Feed 返回 true 时 out 是一条完整有效且可交业务判断的条码 */
typedef struct {
    uint8_t data[23];
    uint8_t used;
    bool last_cr, discard;
} BarcodeRx;
void BarcodeRx_Init(BarcodeRx *r);
void BarcodeRx_Invalidate(BarcodeRx *r); /* 丢弃到下一组完整 CRLF */
bool BarcodeRx_Feed(BarcodeRx *r, uint8_t byte, bool busy, Barcode *out);

/* host_protocol.h */
typedef enum { CMD_NONE, CMD_YELLOW, CMD_GREEN, CMD_RED, CMD_PASS } HostCommand;
typedef struct { uint8_t window[8]; uint8_t used; } HostParser;
extern const uint8_t BARCODE_ACK[7];
extern const uint8_t BARCODE_RESCAN[3];
void HostParser_Init(HostParser *p);
HostCommand HostParser_Feed(HostParser *p, uint8_t byte);

/* barcode_app.h */
typedef enum { APP_IDLE, APP_WAIT_RESULT, APP_RELEASING } AppState;
typedef enum { VIEW_IDLE, VIEW_WAIT, VIEW_RELEASE, VIEW_FAILED, VIEW_DUPLICATE } ViewState;
typedef struct {
    AppState state;
    ViewState view;
    Barcode current;
    uint16_t count;
} AppSnapshot;
void BarcodeApp_Init(uint32_t t0);
void BarcodeApp_OnBarcode(const Barcode *b);
void BarcodeApp_OnCommand(HostCommand cmd, uint32_t handled_at);
void BarcodeApp_Tick(uint32_t now);
bool BarcodeApp_IsBusy(void);
void BarcodeApp_GetSnapshot(AppSnapshot *out);

/* barcode_port.h；发送函数成功意味着已复制进完整帧队列 */
bool BarcodePort_SendHost(const uint8_t *data, uint8_t len);
bool BarcodePort_SendScanner(const uint8_t *data, uint8_t len);
void BarcodePort_SetRelay(bool active);
void BarcodePort_SetLights(bool red, bool green, bool yellow);
void BarcodePort_Init(void);
void BarcodePort_Poll(void);

/* barcode_view.h */
void BarcodeView_Format(const AppSnapshot *s, char lines[6][15]);
void BarcodeView_Poll(void);

/* OLED 新增接口；每次只刷新不超过 budget 个数据字节，命令开销另计 */
void OLED_RefreshStep(uint8_t budget);
```

`BarcodeApp` 的内部状态和 FIFO 使用静态对象；不把 3304 字节 FIFO 放在调用栈。`Barcode` 不是 C 字符串，不可对原始条码调用 `strlen` / `strcmp`。不把 `len` 当作结束符位置覆盖内容。

## Task 1：成功记录 FIFO 与主机测试入口

**Files:** 创建 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/barcode_store.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_store.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_store.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/run.ps1`。

**Interfaces:** 产出上述 `Barcode`、`BarcodeStore` 和三个 Store 函数；不依赖 HAL。

- [ ] 1. 写入第一条失败测试，覆盖长度、字节精确比较与初始化。测试主函数包含以下断言，并打印 `PASS store`：

```c
BarcodeStore s;
Barcode a = {3, {'A', ' ', 'b'}};
Barcode b = {3, {'A', ' ', 'B'}};
BarcodeStore_Init(&s);
assert(s.count == 0);
assert(!BarcodeStore_Contains(&s, &a));
BarcodeStore_Add(&s, &a);
assert(s.count == 1);
assert(BarcodeStore_Contains(&s, &a));
assert(!BarcodeStore_Contains(&s, &b));
BarcodeStore_Add(&s, &a);
assert(s.count == 1);
```

- [ ] 2. 创建测试脚本，以 `-Suite store|rx|protocol|app|port|view|all` 选择套件；编译输出写入 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/MDK-ARM/build_tmp/barcode/`。编译命令为下列形式；任何 GCC 或 EXE 非零状态立即抛错，不能继续打印通过：

```powershell
& 'G:\MinGW64\mingw32\bin\gcc.exe' -std=c99 -Wall -Wextra -Werror -pedantic -ICore/INC tests/barcode/test_store.c Core/SRC/barcode_store.c -o MDK-ARM/build_tmp/barcode/test_store.exe
if ($LASTEXITCODE -ne 0) { throw 'store compilation failed' }
& '.\MDK-ARM\build_tmp\barcode\test_store.exe'
if ($LASTEXITCODE -ne 0) { throw 'store assertions failed' }
```

- [ ] 3. 运行 `powershell -NoProfile -File .\tests\barcode\run.ps1 -Suite store`，先得到缺少实现的失败，再实现长度＋`memcmp` 比较；插入算法如下：

```c
if (b->len == 0u || b->len > BARCODE_MAX_LEN || BarcodeStore_Contains(s, b)) return;
if (s->count < BARCODE_STORE_CAPACITY) {
    s->entries[(s->head + s->count) % BARCODE_STORE_CAPACITY] = *b;
    ++s->count;
} else {
    s->entries[s->head] = *b;
    s->head = (uint16_t)((s->head + 1u) % BARCODE_STORE_CAPACITY);
}
```

- [ ] 4. 增加生成 151 个不同的两字节条码的循环，验证最早记录淘汰、第二条保留、重复不前移、再初始化清零；重跑预期 `PASS store`。映射 T02、T10、T11、T14、T18。
- [ ] 5. 检查本任务文件差异；记录测试命令与结果，不暂存或提交其他原有修改。

## Task 2：完整条码接收与忙时整帧丢弃

**Files:** 创建 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/barcode_rx.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_rx.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_rx.c`；扩展 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/run.ps1` 的 rx 源文件列表。

**Interfaces:** 消费 `Barcode`；产出 `BarcodeRx_Init/Invalidate/Feed`。由事件调度提供字节到达时对应的 busy，不可拿延迟消费时的空闲状态替代。

- [ ] 1. 先写分包测试：

```c
BarcodeRx r; Barcode out;
BarcodeRx_Init(&r);
assert(!BarcodeRx_Feed(&r, 'A', false, &out));
assert(!BarcodeRx_Feed(&r, '\r', false, &out));
assert(BarcodeRx_Feed(&r, '\n', false, &out));
assert(out.len == 1 && out.data[0] == 'A');
assert(!BarcodeRx_Feed(&r, '\r', false, &out));
assert(!BarcodeRx_Feed(&r, '\n', false, &out));
```

- [ ] 2. 运行 `powershell -NoProfile -File .\tests\barcode\run.ps1 -Suite rx`，确认缺少实现而失败。
- [ ] 3. 实现 Feed：每字节先将 `discard |= busy`；一直跟踪 `last_cr`，只有相邻 CRLF 才结束一帧。23 字节缓冲可容纳 21 内容＋CRLF；溢出只设置 discard，继续找分隔符，不回绕写缓冲。结束时仅在 `!discard` 且内容长度 1–21 时复制到 out，然后重置解析器。独立 CR 或 LF 保留为内容字节，不擅自清洗。

```c
/* 结束条件判定先于 last_cr 更新；即使 discard 也必须执行 */
bool ended = r->last_cr && byte == 0x0Au;
r->discard = r->discard || busy;
r->last_cr = byte == 0x0Du;
/* 存储受 sizeof(r->data) 限制；ended 后统一校验并复位 */
```

- [ ] 4. 增加 21／22／100 字节、CRLF 跨调用、连续两条、内含空格和大小写、忙时开始空闲时结束、空闲开始忙时结束、超长后接正常条码测试。对忙时跨边界帧整条丢弃是防止尾部误上传的工程处理，不是新增排队规则。预期 `PASS rx`。映射 T03、T04、T12–T14、T19。
- [ ] 5. 检查所有路径最多复制 23 字节，超长计数不得发生 uint8_t 回绕；记录结果。

## Task 3：四条主机命令的精确识别

**Files:** 创建 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/host_protocol.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/host_protocol.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_protocol.c`；扩展测试脚本。

**Interfaces:** 消费单字节；产出一个 HostCommand 或 CMD_NONE，不自行操作 GPIO 或发送 ACK。

- [ ] 1. 用以下固定帧建立测试，每帧的前七字节都应返回 CMD_NONE，第八字节返回对应命令：

```c
static const uint8_t frames[4][8] = {
    {0xAA,0x08,0xFA,0x01,0x06,0x2F,0xD4,0xEA},
    {0xAA,0x08,0xFA,0x01,0x06,0x31,0x27,0x15},
    {0xAA,0x08,0xFA,0x01,0x06,0x1F,0xE2,0xB9},
    {0xAA,0x08,0xFA,0x01,0x03,0x0A,0x5F,0xD8}
};
const uint8_t BARCODE_ACK[7] = {0xAB,0x07,0xFA,0x01,0x0A,0x30,0x52};
const uint8_t BARCODE_RESCAN[3] = {0x16,0x54,0x0D};
```

- [ ] 2. 运行 `powershell -NoProfile -File .\tests\barcode\run.ps1 -Suite protocol`，确认红灯／PASS 未实现而失败。
- [ ] 3. 使用 8 字节滑动窗口按完整数组匹配；匹配后清空窗口，未匹配则继续滑动找下一帧。不猜测其他协议命令，不据长度字段分配内存，不恢复旧 CRC 算法作为新增接受路径。
- [ ] 4. 测试四帧连续输入、所有拆分位置、噪声前缀、额外 AA、错误末字节及错误帧后紧接有效帧；坏帧不得产出动作，后续好帧必须恢复。预期 `PASS protocol`，覆盖 T17。
- [ ] 5. 对照需求文档逐字节检查常量，记录差异审阅结论。

## Task 4：业务状态机、继电器与灯光时序

**Files:** 创建 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/barcode_app.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_app.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/barcode_port.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_app.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/fake_port.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/fake_port.c`；扩展测试脚本。

**Interfaces:** 消费 Store、HostCommand；调用四个 Port 输出函数；产出 App API。fake_port 实现相同 Port 输出函数，复制每帧数据并记录动作顺序，提供 `FakePort_Reset()`、`FakePort_HostFrames()`、`FakePort_ScannerFrames()`、`FakePort_Relay()` 及可设的发送成功标志；这些测试函数在 fake_port.h 声明。

- [ ] 1. 先写放行不能重复消费的测试：

```c
Barcode a = {1, {'A'}}; AppSnapshot s;
FakePort_Reset(); BarcodeApp_Init(0);
BarcodeApp_OnBarcode(&a);
BarcodeApp_GetSnapshot(&s);
assert(s.state == APP_WAIT_RESULT);
assert(FakePort_HostFrames() == 1);
BarcodeApp_OnCommand(CMD_PASS, 10);
BarcodeApp_GetSnapshot(&s);
assert(s.state == APP_RELEASING && s.count == 1);
assert(FakePort_Relay());
BarcodeApp_OnCommand(CMD_PASS, 500);
BarcodeApp_OnCommand(CMD_RED, 600);
BarcodeApp_Tick(1009); assert(FakePort_Relay());
BarcodeApp_Tick(1010); assert(!FakePort_Relay());
assert(FakePort_ScannerFrames() == 0);
```

- [ ] 2. 运行 `powershell -NoProfile -File .\tests\barcode\run.ps1 -Suite app`，确认尚无业务实现而失败。
- [ ] 3. 实现状态转换，严格保持动作顺序：新码复制 current → 设置 WAIT → 永久取消 boot → 原内容＋CRLF 上传；PASS 在 WAIT 时入库 → 设置 RELEASE → 记录吸合时刻 → 吸合；红灯在 WAIT 时设置 IDLE/失败提示 → 发一次重扫。每个有效命令都排入一个 ACK，其他状态的 PASS 不产生业务动作。原始输出组装如下：

```c
uint8_t wire[23];
memcpy(wire, current.data, current.len);
wire[current.len] = 0x0D;
wire[current.len + 1u] = 0x0A;
/* Port 必须复制数据，不能异步持有这个栈指针 */
(void)BarcodePort_SendHost(wire, (uint8_t)(current.len + 2u));
```

- [ ] 4. 实现独立灯光状态：黄命令立即亮黄且关闭红绿；以命令时间为相位，每 500 ms 翻转，晚调用按已过相位计算而非只翻转一次；绿／红取消黄闪。PASS 不调用灯光变更函数。计时使用无符号差值：

```c
uint32_t elapsed = (uint32_t)(now - release_started);
if (state == APP_RELEASING && elapsed < 0x80000000u && elapsed >= 1000u) {
    BarcodePort_SetRelay(false);
    state = APP_IDLE;
    view = VIEW_IDLE;
}
```

- [ ] 5. 增加完整状态×四命令矩阵、等待期同码异码、失败后重扫一次及可再传、空闲重复不重扫、所有 ACK 字节、绿灯不成功、PASS 不改灯、999／1000 ms、计时跨 `UINT32_MAX`、其他成功记录保留测试；预期 `PASS app`。覆盖 T01–T09、T15–T22。
- [ ] 6. 对发送失败制定保守实现：HAL_BUSY 不丢帧，已接受的扫码不能因发送失败自动解锁或再次上传；队列/硬件异常应暴露故障并停止接受新任务，不伪造 ACK 或成功记录。此为异常保护策略而非用户新增正常业务规则；必须用故障注入验证，不能吞掉上述返回值后继续声称发送成功。

## Task 5：上电三次重扫及不可恢复的取消

**Files:** 修改 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_app.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_app.c`。

**Interfaces:** 沿用 `BarcodeApp_Init(t0)`、`Tick(now)` 和 `OnBarcode`；不增加第四个业务状态。

- [ ] 1. 写时序测试并先运行失败：

```c
FakePort_Reset(); BarcodeApp_Init(2000);
BarcodeApp_Tick(2299); assert(FakePort_ScannerFrames() == 0);
BarcodeApp_Tick(2300); assert(FakePort_ScannerFrames() == 1);
BarcodeApp_Tick(2399); assert(FakePort_ScannerFrames() == 1);
BarcodeApp_Tick(2400); assert(FakePort_ScannerFrames() == 2);
BarcodeApp_Tick(2500); assert(FakePort_ScannerFrames() == 3);
BarcodeApp_Tick(9000); assert(FakePort_ScannerFrames() == 3);
```

- [ ] 2. 增加 `boot_sent`、`boot_cancelled`、`boot_started`；由 t0 计算第 n 次时间 `300 + 100*n`，最多三次。发送排入后才递增次数，不使用 HAL_Delay，不借识别失败重置计数。
- [ ] 3. 在接受条码时、调用上传前设置 `boot_cancelled=true`。测试 299、350、450 ms 接受条码后的剩余次数；无效帧不取消；取消后失败只发一次，后续 Tick 不恢复启动序列。
- [ ] 4. 主循环正常负载必须及时服务 300／400／500 ms。若服务严重迟到，不用 while 循环同一时刻突发补发三帧；每次最多安排一帧并记录调度违约，板上测量不能把迟到补发判为时序通过。
- [ ] 5. 重跑 `powershell -NoProfile -File .\tests\barcode\run.ps1 -Suite app`，预期 `PASS app`；覆盖 T23–T28，增加 t0 回绕测试。

## Task 6：HAL 适配、事件顺序和传输完整性

**Files:** 创建 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_port.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_port.c`；按需要补充测试专用 HAL 替身头 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/hal_stub.h`；扩展测试脚本。

**Interfaces:** 实现 Port API，使用现有 `husart1`、`husart2`。唯一的 `HAL_UART_RxCpltCallback`、`HAL_UART_TxCpltCallback`、`HAL_UART_ErrorCallback` 放在 port 模块；不重定义 IRQ Handler。

- [ ] 1. 先写交错事件测试序列：A 等待 → B 完整扫码 → 红灯 → 再一条 C；预期 B 被丢弃、红灯只发一次重扫、C 原样上传。另测放行 999 ms 开始的扫码在 1001 ms 完成，整帧必须丢弃；不能把忙时缓冲数据等空闲后上传。
- [ ] 2. 用统一 128 槽 RX 事件环存放 `{uint32_t received_at; uint8_t channel; uint8_t byte;}`；两个 UART 中断当前同抢占优先级，生产端仍用保存/恢复 PRIMASK 的极短临界区保护发布索引，消费端只复制一条事件，不关中断处理整批。事件按实际 ISR 入队顺序全局排序，同一毫秒按入队先后处理。
- [ ] 3. 实现事件时间规则：先处理到达时间更早的事件，再推进到现在，不允许先调用 `Tick(当前时刻)` 解锁再处理历史扫码。每个字节处理前以 received_at 推进已有放行截止点；PASS 真正处理并拉低引脚时以 handled_at 记录实际吸合起点，不能因队列迟到缩短物理脉冲。忙时标志交给 BarcodeRx 保持到该帧 CRLF；红灯之前已到达的扫码不得被红灯后的 IDLE 接受。
时间顺序比较采用小于半个 uint32 周期的有效窗口。历史事件时间可能早于实际吸合起点，此时不得把无符号相减产生的大值误判成已经放行到期；上述 elapsed 的半周期检查必须保留。增加“PASS 于10 ms到达、20 ms处理吸合，随后消费15 ms到达的扫码”测试，预期仍在放行而不是提前释放。正常调度持续运行，不能以等待时间无限为由让硬件定时器超过半周期不服务。

- [ ] 4. 上电重扫不能在回放历史事件时补发：在准备接受有效扫码时先取消，再在本轮事件服务完后以真实时间安排启动任务。若 Task 4 的单一 Tick 同时承担定时推进与输出，在此拆成模块内两个静态函数：状态截止推进、当前时刻输出调度；公共 Tick 用于无历史积压时调用。增加 299 ms 条码在 301 ms 消费仍取消首发的测试。
- [ ] 5. 两路 TX 分开使用复制式完整帧 FIFO。上位机 16 槽×最大 23 字节，扫码枪 4 槽×3 字节；在途数据在完成回调前不可覆盖。ACK 不插进条码中间；HAL_BUSY 留在队首重试，错误不冒充完成。不在 RX 中断发送、等待、刷屏或搜索 FIFO。
- [ ] 6. 测试容量边界、栈缓冲复制、TX 完成次序、HAL_BUSY、RX ORE/FE/NE、接收重挂失败。字节丢失后将扫码解析器作废到下一 CRLF，主机解析器清空并重新找完整匹配；事件环溢出标记通信故障，不拼接残片成条码或 PASS。传输状态不确定时停止新上传，继电器已开始的正常脉冲仍由原截止点释放，不自动重扫或清库。故障需在验证记录中可观察；不假装有限队列能应答无限洪泛。
- [ ] 7. 使用 GPIO 宏输出：

```c
void BarcodePort_SetRelay(bool active)
{
    HAL_GPIO_WritePin(KEY_Con_Port, KEY_Con_Pin,
                      active ? GPIO_PIN_RESET : GPIO_PIN_SET);
}
```

三色灯适配先关非目标灯，再开目标灯；保留上电原灯光配置，不在 App_Init 引入额外灯效。
- [ ] 8. 运行 `powershell -NoProfile -File .\tests\barcode\run.ps1 -Suite port`，预期 `PASS port`；再运行 `-Suite all`。必须包括历史积压、跨状态半帧、计时回绕和原字节发送测试，不只验证 HAL API 被调用。

## Task 7：OLED 布局与非阻塞刷新

**Files:** 创建 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/INC/barcode_view.h`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/barcode_view.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/test_view.c`；局部修改 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/OLED/oled.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/OLED/oled.h`。

**Interfaces:** 读取 AppSnapshot，只生成显示数据，不写业务状态；调用既有 OLED_ShowChar(x,y,c,8,1) 与新增 OLED_RefreshStep。

- [ ] 1. 写 `BarcodeView_Format` 失败测试：每行 14 个 ASCII 字符＋NUL，共六行；状态映射为 `IDLE`、`RECOGNIZING`、`RELEASING`、`FAILED`、`DUPLICATE`。采用现有 ASCII 字模，不新增中文字库；这些英文标签分别对应已确认中文状态语义。
- [ ] 2. 固定排版如下。首行 y=0 状态，第二行 y=8 `COUNT:150/150`，第三行 y=16 `BARCODE:`，第四行 y=24 条码前14字符，第五行 y=32 后7字符，第六行 y=40 留空。显示不可打印字节时替换为 `.`，仅操作显示副本。

```text
RECOGNIZING
COUNT:001/150
BARCODE:
12345678901234
5678901

```

- [ ] 3. 在 `BarcodeView_Format` 测试 0／150 计数、21字符分行、短码覆盖长码后的空白清除、非打印字节不修改输入；等待和放行的额外扫码不更新 snapshot。失败和重复提示保留到下一次业务显示更新，但绝不阻止新扫码。
- [ ] 4. 对现有 OLED_Refresh 的页地址与偏移计算做原样复用，新增有游标的 RefreshStep，每次最多 4 个数据字节；页切换只写必要地址命令。不要每帧同步调用完整 OLED_Refresh；不要调用含关中断区的 OLED_ScrollTextTick。
- [ ] 5. View_Poll 在串口事件与计时服务之后运行；快照变化才绘制，单轮最多绘制一个字符／刷新一个片段。新快照打断旧排版时重置绘制游标并清理剩余字符；不复制第二份 OLED 全屏缓冲，不在主循环刷屏期间关中断。
- [ ] 6. 运行 `powershell -NoProfile -File .\tests\barcode\run.ps1 -Suite view`，预期 `PASS view`；板上测量 RefreshStep 与单轮最大耗时，以关键服务间隔不超过 1 ms 为工程验证目标，超标缩小片段而不是延长放行设定值。此目标不是未经测量即可宣称的硬件精度。覆盖 T29、T30。

## Task 8：主循环、工程引用与整体验证

**Files:** 修改 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/Core/SRC/main.c`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/MDK-ARM/T1.uvprojx`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/README.md`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/empty_project/run.ps1`；创建 `G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/tests/barcode/check_project.ps1`、`G:/PY32/Pro/RS-Con/T1/.claude/worktrees/suspicious-blackburn-5fde4d/docs/superpowers/verification/2026-09-19-barcode-dedup.md`。

**Interfaces:** 消费之前所有模块，不增加协议功能。

- [ ] 1. 新结构检查先断言主循环启动新业务、所有新源文件只引用一次、两路 IRQ 转发存在、旧业务引用不存在；初次运行应失败。将旧空工程检查改为显式迁移入口，不保留“main 必须空循环”“不得 OLED/Receive_IT”的冲突断言。
- [ ] 2. 保留现有外设初始化，然后在 USER CODE 区接入以下流程；头文件包含 barcode_app.h、barcode_port.h、barcode_view.h、oled.h。Port_Init 只做队列初始化和接收启动，不调用业务处理；从 t0 到进入循环不插入阻塞初始化。

```c
OLED_Init();
/* OLED 内部初始化延时结束之后，才建立本次上电基准 */
BarcodeApp_Init(HAL_GetTick());
BarcodePort_Init();
while (1) {
    BarcodePort_Poll(); /* 按事件时间推进、处理完事件后调度当前时刻定时输出 */
    BarcodeView_Poll();
}
```

- [ ] 3. 在工程 Core 分组加入六个新 C 文件；核对 ARMCC 能编译接口和 C99 语法，无重复 HAL 回调或重复 main。不要复制整份工程 XML 覆盖用户已有设置。
- [ ] 4. 依次运行以下命令，预期结构检查通过、六个主机套件通过；逐项把 T01–T30 映射到测试函数或板上记录，不把未执行用例标为通过：

```powershell
powershell -NoProfile -File .\tests\barcode\check_project.ps1
powershell -NoProfile -File .\tests\barcode\run.ps1 -Suite all
powershell -NoProfile -File .\tests\empty_project\run.ps1
```

- [ ] 5. Keil 全量重建并检查日志实际错误／警告数字和产物时间；进程退出码不能替代日志验收：

```powershell
$project = Join-Path (Get-Location).Path 'MDK-ARM\T1.uvprojx'
$log = Join-Path (Get-Location).Path 'MDK-ARM\build_tmp\barcode\keil-rebuild.log'
$p = Start-Process -FilePath 'G:\keil5\UV4\UV4.exe' -ArgumentList @('-r', ('"' + $project + '"'), '-t', 'T1', '-o', ('"' + $log + '"')) -WindowStyle Hidden -Wait -PassThru
Get-Content -LiteralPath $log
```

预期 `0 Error(s), 0 Warning(s)`。记录 Code、RO-data、RW-data、ZI-data，核对链接 map 的栈堆和硬件实际型号容量。当前工程 XML 声明 16 KiB RAM／128 KiB Flash，这只是工程配置，不能当作芯片真实容量已确认。新增数据初步预算：Store 3304 B、RX 环约1024 B、TX及状态约1 KiB、既有 OLED 528 B；最终以目标编译大小和实物容量为准，不为链接通过擅自扩大内存区。
- [ ] 6. 将板上验收单写入验证记录但初始标为“未执行”：逻辑分析仪核对两路 9600 8N1 原字节、四 ACK、t0 后 300/400/500 ms、取消剩余重扫、失败重扫一次；测 PA12 低电平持续1000 ms、放行重复PASS及红灯不改截止点；实测黄灯500/500、三灯互斥、OLED全21字符、连续收发不丢字节；完成150/151 FIFO与重启清零。t0 可用调试器时间标记，不擅自占用新 GPIO。
- [ ] 7. 更新 README，解释 PASS≠绿灯、无识别超时、FIFO不是永久唯一、防重不跨重启、协议缺少事务号无法解决跨轮次迟到结果；区分正常行为、通信故障保护和未执行板上验收。
- [ ] 8. 最终检查 `git diff --check` 和本次修改列表；已有历史差异导致的问题单独注明，不擅自修复无关文件。完成后交付实际测试证据及剩余硬件验收项，不自动提交、不烧录。

## 验收映射与审阅结论

| 需求用例 | 主要实施任务 |
|---|---|
| T01–T09 | Task 1、2、4、6 |
| T10–T14 | Task 1、2、8 |
| T15–T18 | Task 3、4、6 |
| T19–T22 | Task 2、4、6 |
| T23–T28 | Task 5、6、8 |
| T29–T30 | Task 7、8 |

额外工程回归：跨状态半帧、积压事件顺序、uint32 时间回绕、主机噪声重同步、RX 溢出、TX_BUSY／失败、OLED分片延迟、实际 RAM/Flash 容量。以上不能替代 T01–T30。

本计划采用完整条码和固定 FIFO，而不是仅用哈希；使用单状态机而不是条码任务队列；OLED采用分行而不是滚动，减少资源占用及对串口时序的干扰。传输缓冲只是字节/输出帧缓冲，不代表允许忙时扫码排队等待识别。

## 执行交接

需求已确认，计划已编写，尚未开始实现。建议用户同意后在本会话按 executing-plans 顺序实施，每个任务执行“失败测试→最小实现→测试通过→差异检查”。如用户明确选择子代理方式，再启用相应技能；当前未启动任何子代理。只在用户授权提交时逐任务暂存精确文件并提交，禁止 `git add .` 混入原有工作区修改。