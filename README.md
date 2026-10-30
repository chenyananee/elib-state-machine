**简体中文** | [English](README.en.md)

# elib-state-machine

嵌入式状态机框架，四种独立模式，单公共头。

elibs 家族成员：零动态内存、零全局变量、C99、单头伞公共 API、统一错误码、用户持有 ctx、非法配置 init 即拒绝。

## 特性

- **Switch/Case 状态机** (`elib_fsm`)：轻量级状态跟踪器，支持立即/延迟跳转
- **回调状态机** (`elib_fsm_cb`)：entry/exit/run/event 回调调度、按需事件分发、延迟跳转、临界期事件转交
- **层次状态机** (`elib_fsm_hsm`)：父子状态、事件冒泡、外部跳转（严格 LCA 域）语义
- **带历史的层次状态机** (`elib_fsm_hsm_hist`)：深历史，`goto` 自动恢复上次活跃的子状态；`reset` 一键回初始
- **init 全量结构校验**：状态表在 `init` 时一次性校验——环、超深、重复值、保留值 `-1`、悬垂引用、非直系 initial，全部拒绝；非法配置活不到运行时
- **全部遍历迭代化 + 防线上界**：即使运行期被改坏的历史表也无法让库死循环（异常直接报错返回，绝不挂死）
- 零动态内存分配，用户分配上下文，无构建系统依赖

## 编译

无构建系统（家族规范）。按所用模式挑选对应源文件与公共头一起编译即可，四模式互不依赖：

```bash
# Switch/Case 状态机
gcc -std=c99 -c elib_fsm_core.c -I include

# 回调状态机
gcc -std=c99 -c elib_fsm_cb_core.c -I include

# 层次状态机
gcc -std=c99 -c elib_fsm_hsm_core.c -I include

# 带历史的层次状态机
gcc -std=c99 -c elib_fsm_hsm_hist_core.c -I include
```

**自定义最大层次深度**：通过编译期宏覆盖（**不要修改库文件**）。hsm 与 hist 模式的库代码和使用方必须使用**同一份**定义，不一致属未定义行为：

```bash
gcc -std=c99 -DELIB_FSM_HSM_MAX_DEPTH=32 -c elib_fsm_hsm_core.c -I include
gcc -std=c99 -DELIB_FSM_HSM_MAX_DEPTH=32 -c elib_fsm_hsm_hist_core.c -I include
```

未定义时使用默认值 `16`。

## 快速入门

### Switch/Case 状态机

定义状态，在主循环中用 `elib_fsm_poll()` 驱动：

```c
#include "elib_fsm.h"

enum { STATE_IDLE, STATE_ACTIVE, STATE_ERROR };

elib_fsm_ctx_t ctx;
elib_fsm_init(&ctx, STATE_IDLE);

while (1) {
    switch (elib_fsm_poll(&ctx, 10)) {  /* 10ms tick */
        case STATE_IDLE:
            if (button_pressed)
                elib_fsm_goto(&ctx, STATE_ACTIVE, 50);  /* 50ms 消抖 */
            break;
        case STATE_ACTIVE:
            handle_active();
            break;
        default:
            /* 延迟等待中（poll 返回 -1） */
            break;
    }
}
```

### 回调状态机

定义带回调的状态描述符（const 表可放 Flash）：

```c
#include "elib_fsm.h"

static void on_entry(elib_fsm_state_t state, void *user_data) { /* ... */ }
static void on_exit(elib_fsm_state_t state, void *user_data) { /* ... */ }
static void on_run(void *user_data) { /* ... */ }
static void on_event(void *event_data, void *user_data) { /* ... */ }

static const elib_fsm_cb_state_desc_t states[] = {
    { STATE_IDLE,   on_entry, on_exit, on_run, on_event },
    { STATE_ACTIVE, on_entry, on_exit, on_run, on_event },
};

elib_fsm_cb_ctx_t ctx;
/* 最后一个参数为临界状态事件回调，NULL 表示临界期事件直接丢弃 */
elib_fsm_cb_init(&ctx, states, 2, STATE_IDLE, NULL, NULL);

elib_fsm_cb_goto(&ctx, STATE_ACTIVE, 0);   /* exit(IDLE) -> entry(ACTIVE) */
elib_fsm_cb_poll(&ctx, 10, &msg);          /* 先调 event(msg)，再调 run */

/* 按需投递事件（不触发 run，一个 tick 内可多次调用） */
elib_fsm_cb_dispatch(&ctx, &key_event);
```

### 层次状态机

定义带层次关系的描述符，事件沿活跃路径冒泡：

```c
#include "elib_fsm.h"

enum { ST_OFF, ST_ON };
enum { EVT_CMD_POWER, EVT_CMD_TIMEOUT };

static bool off_handler(void *event_data, void *user_data) {
    elib_fsm_hsm_ctx_t *fsm = (elib_fsm_hsm_ctx_t *)user_data;
    if (*(int *)event_data == EVT_CMD_POWER) {
        elib_fsm_hsm_goto(fsm, ST_ON);
        return true;
    }
    return false;
}

static bool on_handler(void *event_data, void *user_data) {
    elib_fsm_hsm_ctx_t *fsm = (elib_fsm_hsm_ctx_t *)user_data;
    if (*(int *)event_data == EVT_CMD_TIMEOUT) {
        elib_fsm_hsm_goto(fsm, ST_OFF);
        return true;
    }
    return false;
}

static const elib_fsm_hsm_state_desc_t states[] = {
    /*  state    parent                 initial                entry  exit  run   handler      */
    {   ST_OFF,  ELIB_FSM_STATE_INVALID, ELIB_FSM_STATE_INVALID, NULL, NULL, NULL, off_handler },
    {   ST_ON,   ELIB_FSM_STATE_INVALID, ELIB_FSM_STATE_INVALID, NULL, NULL, NULL, on_handler  },
};

elib_fsm_hsm_ctx_t fsm;
elib_fsm_hsm_init(&fsm, states, 2, ST_OFF, &fsm);

int evt = EVT_CMD_POWER;  elib_fsm_hsm_dispatch(&fsm, &evt);  /* OFF -> ON */
evt = EVT_CMD_TIMEOUT;    elib_fsm_hsm_dispatch(&fsm, &evt);  /* ON  -> OFF */
```

### 带历史的层次状态机

与标准 HSM 相同的层次结构，但 `goto` 自动恢复上次离开时的子状态（深历史）：

```c
#include "elib_fsm.h"

enum { ST_ROOT, ST_A, ST_B, ST_A1, ST_A2, ST_B1, ST_B2 };

/* 注意：不加 const —— 框架运行时写 last_active，此表必须放 RAM */
static elib_fsm_hsm_hist_state_desc_t states[] = {
    /*  state      parent    initial  last_active  entry     exit      run    handler  */
    {   ST_ROOT,   INVALID,  ST_A,    INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_A,      ST_ROOT,  ST_A1,   INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_B,      ST_ROOT,  ST_B1,   INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_A1,     ST_A,     INVALID, INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_A2,     ST_A,     INVALID, INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_B1,     ST_B,     INVALID, INVALID,     NULL,     NULL,     NULL,  NULL     },
    {   ST_B2,     ST_B,     INVALID, INVALID,     NULL,     NULL,     NULL,  NULL     },
};

elib_fsm_hsm_hist_ctx_t fsm;
elib_fsm_hsm_hist_init(&fsm, states, 7, ST_ROOT, NULL);

/* ROOT -> A -> A1（initial 链） */
elib_fsm_hsm_hist_goto(&fsm, ST_A2);     /* A.last_active = A2 */
elib_fsm_hsm_hist_goto(&fsm, ST_B);      /* B 无历史，走 initial -> B1 */
elib_fsm_hsm_hist_goto(&fsm, ST_A);      /* 恢复到 A2！（A.last_active = A2） */
elib_fsm_hsm_hist_reset(&fsm, ST_ROOT);  /* 清空全部历史，回到干净的初始叶子 A1 */
```

## init 结构校验矩阵

三种带表的模式（`fsm_cb` / `hsm` / `hsm_hist`）在 `init` 时对整张状态表做全量校验。**校验失败时 ctx 一字不动、回调一个不调**，直接返回错误码：

| 违规 | 错误码 |
|------|--------|
| 状态值重复（查找不确定） | `ELIB_FSM_ERR_INVALID_PARAM` |
| 状态值 == `ELIB_FSM_STATE_INVALID`（-1 是保留值，会污染"无状态"信号） | `ELIB_FSM_ERR_INVALID_PARAM` |
| `parent` / `initial` 指向表外状态（悬垂引用） | `ELIB_FSM_ERR_STATE_NOT_FOUND` |
| parent 链成环，或链深超过 `ELIB_FSM_HSM_MAX_DEPTH` | `ELIB_FSM_ERR_INVALID_PARAM` |
| 叶子状态填了 `initial`；复合状态的 `initial` 不是自己的直系子状态 | `ELIB_FSM_ERR_INVALID_PARAM` |
| `initial`（种子状态）不在表内 | `ELIB_FSM_ERR_STATE_NOT_FOUND` |

约束含义：

- **深度不变量**：一个状态的 parent 链最多允许 `ELIB_FSM_HSM_MAX_DEPTH - 1` 个 parent 跳数到达顶层。环走不到头、链超出上界，两种违规在这一条里**一并拒绝**——v1 里"环是 UB、超深静默截断"的时代结束了
- **initial 必须是直系子**：该约束 + 深度上界使 initial 链天然无环、必达叶子
- 校验为 O(n·depth) 两遍迭代，零动态内存，无递归
- `elib_fsm`（switch/case 模式）没有状态表，无校验可言：用户自持状态值责任，但请同样远离 `-1`（保留值）

运行期防护（纵深防御）：`hsm_hist` 的表不是 const，用户随时可把它改坏。所有路径遍历（find/descend/exit/enter/domain）都做了深度上界与直系检查，发现非法链即返回错误并拒绝处理，绝不死循环。

## 核心语义

### 跳转域（严格 LCA 外部转移）

`hsm` / `hsm_hist` 的 `goto` 是**外部转移**，跳转域 = 源叶子与目标状态的最近公共**严格**祖先：

- 跳到兄弟：只 exit/entry 正在活跃的子树路径
- 跳到祖先或自身：该层级**之上**的环境保留，域是祖先的父态——即祖先状态本身也会被 exit 再 entry（严格语义）
- 跳到顶层状态 / 森林里另一棵树的顶层：无公共祖先，整机退出再重入
- 目标是复合状态时，沿其 `initial` 链直落叶子（hsm），`hsm_hist` 则先查 `last_active` 链恢复历史叶子

`hsm_hist` 的跳转链再写回：`goto` 成功后沿新叶子整条 parent 链回写 `last_active`（直到顶层），保证以后从任何外层复合态下降也走到这条新分支。

### 延迟跳转时序（fsm / fsm_cb）

`goto(target, delay_ms > 0)`：

1. **exit 即刻**：调用即退出源状态（`fsm_cb` 立即触发 `exit` 回调；`current` 进入"无状态"）
2. **entry 到期**：`poll()` 累计的周期覆盖 `delay_ms` 的那一拍，先 `entry(target)`，再让新状态处理本拍事件与 `run`

挂起期间（临界窗口）：

- `current()` / `poll()` 返回 `ELIB_FSM_STATE_INVALID`，`previous()` 仍返回源状态
- 事件交给 init 时注册的 `transition_event` 回调（fsm_cb），未注册则丢弃；`remaining_ms` 参数是**扣除本拍周期之前**的值
- **`poll(period_ms = 0)` 是合法调用**：零时间流逝，延迟倒计时完全不推进（无论调多少次都不会误触发），但临界态事件**仍会投递**——把 `period=0` 的 poll 当纯事件泵用是文档契约
- 挂起期间再次 `goto`：延迟中的目标被替换（不重复 exit）；用 `goto(目标, 0)` 立即落地（源已 exit，**不再触发第二次 exit**）
- 取消一个挂起的延迟转移（不关心原目标是谁）：`goto(previous(), 0)` 直接回到源状态——本库不提供 cancel API，此习语即取消

### dispatch 内嵌 goto 的收敛（hsm / hsm_hist）

handler 里调用 `goto` 是一等用法：转移发生后**冒泡立即停止**（`dispatch` 返回 `true`）。判定依据是 ctx 里的转移计数器 `transition_seq`（每次 goto 成功自增），**不依赖 current 是否变化**——因此即使 goto 解析回**同一个叶子**（自转移，或跳到祖先又恢复历史回到原叶），冒泡同样正确收敛。这是 v2 相对 v1 的行为修正：v1 里 handler 内 goto 同一叶子会让引擎继续向已 exit 的祖先冒泡。

handler 的返回值与goto 的关系：跳转了就停（返回 `true`）；没跳转才看 handler 的 `bool` 返回值。

### init 内 entry 重入

三种带回调模式的 `init` 会先置位 `initialized` 再触发初始状态的 entry（hsm/hist 是整条初始路径自顶向下）——**entry 回调里调用 goto 是合法的**，立即生效。

## API 参考

### 错误码（四模式共用）

| 错误码 | 语义 |
|--------|------|
| `ELIB_FSM_OK` | 成功 |
| `ELIB_FSM_ERR_INVALID_PARAM` | 参数非法（NULL ctx、空表、结构校验失败） |
| `ELIB_FSM_ERR_NOT_INITIALIZED` | ctx 未初始化（或已 deinit）时调用 API |
| `ELIB_FSM_ERR_STATE_NOT_FOUND` | 引用了表外状态值（init 种子、goto 目标、悬垂 parent/initial） |

### Switch/Case 状态机

| 函数 | 说明 |
|------|------|
| `elib_fsm_init(ctx, initial)` | 初始化，设置初始状态（本模式无表，无校验；`initial` 即用户自定义值） |
| `elib_fsm_deinit(ctx)` | 反初始化 |
| `elib_fsm_goto(ctx, target, delay_ms)` | 跳转状态，`delay=0` 立即，`delay>0` 延迟；挂起期间再次 goto 替换目标 |
| `elib_fsm_poll(ctx, period_ms)` | 推进一拍并返回状态；延迟等待中返回 -1；`period_ms=0` 合法（零时间流逝，延迟冻结） |
| `elib_fsm_current(ctx)` | 当前状态；延迟等待中返回 -1 |
| `elib_fsm_previous(ctx)` | 上一个有效状态（延迟期间返回延迟的源状态，不返回 -1） |

### 回调状态机

| 函数 | 说明 |
|------|------|
| `elib_fsm_cb_init(ctx, states, state_count, initial, user_data, transition_event)` | 初始化（含全量校验矩阵，见上）；初始状态的 entry 在 init 内触发 |
| `elib_fsm_cb_deinit(ctx)` | 反初始化 |
| `elib_fsm_cb_goto(ctx, target, delay_ms)` | `delay=0` 立即（exit→entry）；`delay>0` 延迟（exit 即刻、entry 到期）；目标在表外返回 `STATE_NOT_FOUND`，不触发任何回调 |
| `elib_fsm_cb_poll(ctx, period_ms, event_data)` | 一拍：正常拍先 event 后 run；到期拍先 entry(target) 再让新状态 event/run；临界拍事件交 `transition_event`、跳过 event/run；`event_data=NULL` 表示"无事件"，事件回调全跳过、run 照常 |
| `elib_fsm_cb_dispatch(ctx, event_data)` | 按需投递事件（不触发 run，可一拍多次）；临界期同样转交 `transition_event`；`event_data=NULL` 直接返回 OK |
| `elib_fsm_cb_current(ctx)` / `elib_fsm_cb_previous(ctx)` | 同 switch/case 语义 |

**临界状态事件回调 (`transition_event`)**：

| 参数 | 说明 |
|------|------|
| `event_data` | 事件数据指针 |
| `from` | 已退出的源状态 |
| `to` | 待进入的目标状态 |
| `remaining_ms` | 距到期剩余毫秒（扣除本拍周期之前） |
| `user_data` | 用户数据 |

### 层次状态机

状态描述符 (`elib_fsm_hsm_state_desc_t`)：

| 字段 | 说明 |
|------|------|
| `state` | 状态值（唯一，≠ -1） |
| `parent` | 父状态，INVALID = 顶层（必须表内存在，链上界受限，见校验矩阵） |
| `initial` | 默认子状态，叶子必须为 INVALID；复合态必须指向**直系**子状态 |
| `entry` / `exit` | 进入/退出回调（nullable）；hsm 的 `run` 只对**叶子**有意义：poll 只调当前叶子的 run，复合态的 run 被忽略（与家族先例一致，文档声明而非拒绝） |
| `handler` | 事件处理回调（nullable），`bool (*)(void *event_data, void *user_data)`；返回 `true` 表示已处理（停止冒泡） |

| 函数 | 说明 |
|------|------|
| `elib_fsm_hsm_init(ctx, states, state_count, initial, user_data)` | 初始化（全量校验矩阵）；沿 initial 链降到叶子，路径全部 entry 自顶向下在 init 内触发 |
| `elib_fsm_hsm_deinit(ctx)` | 反初始化 |
| `elib_fsm_hsm_goto(ctx, target)` | 外部跳转（严格 LCA 域，见核心语义）；目标在表外返回 `STATE_NOT_FOUND`，机器不动 |
| `elib_fsm_hsm_poll(ctx)` | 一拍：调当前叶子 run，返回叶子状态 |
| `elib_fsm_hsm_dispatch(ctx, event_data)` | 事件沿活跃路径自叶向根冒泡；handler 内 goto 即停（含同叶 goto，见核心语义）；`event_data=NULL` 返回 false |
| `elib_fsm_hsm_current(ctx)` | 当前叶子状态 |

### 带历史的层次状态机

描述符多一字段 `last_active`——**运行期框架管理的深历史**（框架写，用户只读）。整表因此**必须非 const、放 RAM**。

| 函数 | 说明 |
|------|------|
| `elib_fsm_hsm_hist_init(ctx, states, state_count, initial, user_data)` | 初始化（全量校验矩阵同 hsm）；所有 `last_active` 播种为各自的 `initial`，沿链降到叶子 |
| `elib_fsm_hsm_hist_deinit(ctx)` | 反初始化 |
| `elib_fsm_hsm_hist_goto(ctx, target)` | 外部跳转（域语义同 hsm）；叶子解析**历史优先**（`last_active` 链），无历史走 initial 链；解析完成后整条链回写到顶层 |
| `elib_fsm_hsm_hist_reset(ctx, target)` | 先清空**全部**历史（回 `initial`），再以干净链进入 `target` 的解析叶子并回写整链——含跳转域之外、不参与 exit/entry 的祖先；**解析叶子即当前叶子时不触发任何 exit/entry**（纯历史重置）；`target` 在表外返回 `STATE_NOT_FOUND` |
| `elib_fsm_hsm_hist_poll(ctx)` / `elib_fsm_hsm_hist_dispatch(ctx, event_data)` / `elib_fsm_hsm_hist_current(ctx)` | 同 hsm |

**hsm vs hsm_hist 选择**：

```
场景：ROOT → A → A2 → goto(B) → goto(A)

标准 hsm:     goto(A) → 走 initial → 回到 A1（固定初始）
带历史 hsm:   goto(A) → 恢复历史 → 回到 A2（上次位置）
```

- `elib_fsm_hsm`：每次进入复合态都从固定初始开始（协议、简单 UI），7 字段 const 表可放 Flash
- `elib_fsm_hsm_hist`：需要记住离开位置（复杂 UI 页面、多上下文），8 字段非 const 表必须 RAM

## 使用约束

- **保留值 `-1`**：`ELIB_FSM_STATE_INVALID` 按**约定**保留。有表模式 init 直接拒绝撞 `-1` 的状态值；`elib_fsm` 无表，靠用户自律——撞了 `-1` 会让"无状态"信号失真，别做
- **状态表生命周期**：hsm 表可 const 放 Flash（init 后只读遍历）；hsm_hist 表必须 RAM 且**用户不得手工改写** `last_active`（改坏即进入防御路径，`goto`/`dispatch` 返回错误而非挂死）
- **re-entrancy**：回调里调本 ctx 的 API 都是设计内行为（见"init 内 entry 重入"与"dispatch 内嵌 goto"）；不支持多任务并发访问同一 ctx（无锁，单线程上下文使用）
- **上下文尺寸**：ctx 自包含、无外部内存；hsm 路径缓冲区在栈上以 `ELIB_FSM_HSM_MAX_DEPTH` 兜底

## v1 → v2 语义变化

从 v1 升级请注意（ctx 内存布局不兼容，属破坏性重构）：

1. **公共头合一**：九个头（`elib_fsm*.h` 全家）合并为唯一伞头 `include/elib_fsm.h`，include 路径从相对路径改为 `-Iinclude` 直连
2. **错误码改名**：`DELAYED_TRANSITION`（全库无返回点的死码）删除；`INVALID_PARAM` / `NOT_INITIALIZED` / `STATE_NOT_FOUND` 三态语义按上表明确分工（v1 里"结构违规"和"表外引用"混在 `INVALID_PARAM` 里）
3. **init 校验**：v1 对状态表零校验（环是 UB、超深静默截断）；v2 全量校验、全部拒绝且 ctx 零污染
4. **超深表**：v1 静默截断 entry（回调不对称泄漏）；v2 init 拒绝
5. **dispatch 同叶漏判修复**：v1 里 handler 内 goto 回同一叶子会向已 exit 的祖先冒泡；v2 用转移计数器修死（见核心语义）
6. **poll(0) 契约**：v1 中 `period_ms=0` 只是个"碰巧不推进"的实现细节；v2 升格为文档契约——零时间流逝，延迟冻结，临界事件照常投递
7. **取消延迟转移**：不加 cancel API；用 `goto(previous(), 0)` 习语（v1 亦如此，v2 写进文档并配测试）
8. **构建系统**：`CMakeLists.txt` 与 `.github` CI 移除（家族规范：无构建系统，编译命令见上文）
9. **hsm/hist 不提供的功能**（与 v1 一致，明确非目标）：延迟 goto、previous、cancel；UML 内部转移/entry-point/guard/choice 伪状态；事件队列；正交区域

## 测试

宿主编译并运行四套断言测试（每组含 v1 平移用例 + v2 新增：校验矩阵、同叶冒泡回归、poll(0) 契约、reset 短路等）：

```bash
gcc -std=c99 -Wall -Wextra -o test_fsm test/test_elib_fsm.c src/elib_fsm_core.c -I include && ./test_fsm

gcc -std=c99 -Wall -Wextra -o test_fsm_cb test/test_elib_fsm_cb.c src/elib_fsm_cb_core.c -I include && ./test_fsm_cb

gcc -std=c99 -Wall -Wextra -o test_hsm test/test_elib_fsm_hsm.c src/elib_fsm_hsm_core.c -I include && ./test_hsm

gcc -std=c99 -Wall -Wextra -o test_hsm_hist test/test_elib_fsm_hsm_hist.c src/elib_fsm_hsm_hist_core.c -I include && ./test_hsm_hist
```

全部测试以 arm-none-eabi-gcc（`-std=c99 -Wall -Wextra -Werror`）编译通过，断言期望值逐条手工推导。
