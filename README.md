# elib-state-machine

嵌入式应用有限状态机框架。

## 特性

- **Switch/Case 状态机** (`elib_fsm`): 轻量级状态跟踪器，支持立即/延迟跳转
- **回调状态机** (`elib_fsm_cb`): 回调驱动型状态机，支持 entry/exit/run/event 回调调度、按需事件分发与延迟跳转
- **层次状态机** (`elib_fsm_hsm`): HSM 层次状态机，支持父子状态、事件冒泡、LCA 跳转语义
- **带历史的层次状态机** (`elib_fsm_hsm_hist`): 带深历史的 HSM，goto 自动恢复上次活跃的子状态
- 零动态内存分配
- 用户分配上下文
- 四种模式完全解耦，可独立或组合使用

## 快速入门

### Switch/Case 状态机

定义状态，在主循环中使用 `elib_fsm_poll()` 驱动状态机：

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
            /* 延迟等待中 */
            break;
    }
}
```

### 回调状态机

定义带回调的状态描述符，使用 `elib_fsm_cb_goto()` 跳转状态：

```c
#include "elib_fsm_cb.h"

static void on_entry(elib_fsm_state_t state, void *user_data) { /* ... */ }
static void on_exit(elib_fsm_state_t state, void *user_data) { /* ... */ }
static void on_run(void *user_data) { /* ... */ }
static void on_event(void *event_data, void *user_data) { /* ... */ }

static const elib_fsm_cb_state_desc_t states[] = {
    { STATE_IDLE,   on_entry, on_exit, on_run, on_event },
    { STATE_ACTIVE, on_entry, on_exit, on_run, on_event },
};

elib_fsm_cb_ctx_t ctx;
elib_fsm_cb_init(&ctx, states, 2, STATE_IDLE, NULL);

elib_fsm_cb_goto(&ctx, STATE_ACTIVE, 0);   /* exit(IDLE) -> entry(ACTIVE) */
elib_fsm_cb_poll(&ctx, 10, &msg);          /* 10ms tick，先调 on_event(msg)，再调 on_run */

/* 按需投递外部事件（不触发 run，可多次调用） */
elib_fsm_cb_dispatch(&ctx, &key_event);    /* 仅调用 on_event(key_event) */
```

### 层次状态机

定义带层次关系的状态描述符，使用 `elib_fsm_hsm_dispatch()` 投递事件（支持冒泡）：

```c
#include "elib_fsm_hsm.h"

enum { ST_OFF, ST_ON };
enum { EVT_CMD_POWER, EVT_CMD_TIMEOUT };

typedef struct {
    int cmd;
} fsm_event_t;

static bool off_handler(void *event_data, void *ud) {
    fsm_event_t *evt = (fsm_event_t *)event_data;
    if (evt->cmd == EVT_CMD_POWER) { elib_fsm_hsm_goto((elib_fsm_hsm_ctx_t*)ud, ST_ON); return true; }
    return false;
}

static bool on_handler(void *event_data, void *ud) {
    fsm_event_t *evt = (fsm_event_t *)event_data;
    if (evt->cmd == EVT_CMD_TIMEOUT) { elib_fsm_hsm_goto((elib_fsm_hsm_ctx_t*)ud, ST_OFF); return true; }
    return false;
}

static const elib_fsm_hsm_state_desc_t states[] = {
    { ST_OFF, ELIB_FSM_STATE_INVALID, ELIB_FSM_STATE_INVALID, NULL, NULL, NULL, off_handler },
    { ST_ON,  ELIB_FSM_STATE_INVALID, ELIB_FSM_STATE_INVALID, NULL, NULL, NULL, on_handler  },
};

elib_fsm_hsm_ctx_t fsm;
elib_fsm_hsm_init(&fsm, states, 2, ST_OFF, &fsm);

fsm_event_t evt = {0};
evt.cmd = EVT_CMD_POWER; elib_fsm_hsm_dispatch(&fsm, &evt);   /* OFF -> ON */
evt.cmd = EVT_CMD_TIMEOUT; elib_fsm_hsm_dispatch(&fsm, &evt); /* ON -> OFF */
```

### 带历史的层次状态机

与标准 HSM 相同的层次结构，但 `goto` 自动恢复上次离开时的子状态：

```c
#include "elib_fsm_hsm_hist.h"

enum { ST_ROOT, ST_A, ST_B, ST_A1, ST_A2, ST_B1, ST_B2 };

/* 注意：不加 const，框架会修改 last_active */
static elib_fsm_hsm_hist_state_desc_t states[] = {
    /*  state      parent    initial  last_active  entry        exit         run    handler  */
    {   ST_ROOT,   INVALID,  ST_A,    INVALID,     NULL,        NULL,        NULL,  NULL     },
    {   ST_A,      ST_ROOT,  ST_A1,   INVALID,     on_entry,    on_exit,    NULL,  NULL     },
    {   ST_B,      ST_ROOT,  ST_B1,   INVALID,     on_entry,    on_exit,    NULL,  NULL     },
    {   ST_A1,     ST_A,     INVALID, INVALID,     on_entry,    on_exit,    run,   NULL     },
    {   ST_A2,     ST_A,     INVALID, INVALID,     on_entry,    on_exit,    run,   NULL     },
    {   ST_B1,     ST_B,     INVALID, INVALID,     on_entry,    on_exit,    run,   NULL     },
    {   ST_B2,     ST_B,     INVALID, INVALID,     on_entry,    on_exit,    run,   NULL     },
};

elib_fsm_hsm_hist_ctx_t fsm;
elib_fsm_hsm_hist_init(&fsm, states, 7, ST_ROOT, NULL);

/* ROOT -> A -> A1 (initial 链) */
elib_fsm_hsm_hist_goto(&fsm, ST_A2);   /* A.last_active = A2 */
elib_fsm_hsm_hist_goto(&fsm, ST_B);    /* B 无历史，走 initial -> B1 */
elib_fsm_hsm_hist_goto(&fsm, ST_A);    /* 恢复到 A2！（A.last_active = A2） */
elib_fsm_hsm_hist_reset(&fsm);         /* 清除所有历史，回到 A1 */
```

## 使用案例

### 按键消抖（Switch/Case 状态机）

最简单的状态跟踪场景：延迟跳转天然支持消抖定时。

```c
#include "elib_fsm.h"

enum { KEY_STABLE, KEY_PRESSED, KEY_DEBOUNCE };

elib_fsm_ctx_t key_fsm;
elib_fsm_init(&key_fsm, KEY_STABLE);

while (1) {
    switch (elib_fsm_poll(&key_fsm, 5)) {  /* 5ms tick */
        case KEY_STABLE:
            if (key_gpio_low()) {
                elib_fsm_goto(&key_fsm, KEY_PRESSED, 0);    /* 立即跳转 */
                start_debounce_timer();
            }
            break;
        case KEY_PRESSED:
            if (debounce_timer_expired()) {
                confirm_key_press();
                elib_fsm_goto(&key_fsm, KEY_DEBOUNCE, 50);  /* 50ms 消抖延迟 */
            }
            break;
        case KEY_DEBOUNCE:
            stop_debounce_timer();
            elib_fsm_goto(&key_fsm, KEY_STABLE, 0);
            break;
        default:
            break;  /* 延迟等待中 */
    }
}
```

### 通信协议状态机（回调状态机）

串口协议解析的典型场景：进入/离开状态时需要启停硬件，主循环中周期处理数据。

```c
#include "elib_fsm_cb.h"

enum { PROTO_IDLE, PROTO_WAIT_HEADER, PROTO_RECV_DATA, PROTO_CHECKSUM };

typedef struct {
    uint8_t rx_buf[256];
    uint16_t rx_len;
} proto_data_t;

static void proto_entry(elib_fsm_state_t state, void *user_data) {
    proto_data_t *pd = (proto_data_t *)user_data;
    switch (state) {
        case PROTO_IDLE:       pd->rx_len = 0; uart_enable_rx(); break;
        case PROTO_RECV_DATA:  pd->rx_len = 0; break;
        case PROTO_CHECKSUM:   start_checksum_timer(); break;
        default: break;
    }
}

static void proto_exit(elib_fsm_state_t state, void *user_data) {
    (void)user_data;
    switch (state) {
        case PROTO_IDLE:      uart_disable_rx(); break;
        case PROTO_CHECKSUM:  stop_checksum_timer(); break;
        default: break;
    }
}

static void proto_run(void *user_data) {
    proto_data_t *pd = (proto_data_t *)user_data;
    process_rx_buffer(pd);
}

static const elib_fsm_cb_state_desc_t proto_states[] = {
    { PROTO_IDLE,        proto_entry, proto_exit, NULL,      NULL },
    { PROTO_WAIT_HEADER, proto_entry, proto_exit, NULL,      NULL },
    { PROTO_RECV_DATA,   proto_entry, proto_exit, proto_run, NULL },
    { PROTO_CHECKSUM,    proto_entry, proto_exit, NULL,      NULL },
};

proto_data_t proto_data;
elib_fsm_cb_ctx_t proto_fsm;
elib_fsm_cb_init(&proto_fsm, proto_states, 4, PROTO_IDLE, &proto_data);

/* 中断中直接跳转状态 */
elib_fsm_cb_goto(&proto_fsm, PROTO_WAIT_HEADER, 0);

/* 主循环中驱动 */
while (1) {
    elib_fsm_cb_poll(&proto_fsm, 10, NULL);  /* 10ms tick，先调 event 回调再调 run 回调 */
}
```

### 电源管理（回调状态机 + 延迟跳转）

通过 `user_data` 传递设备上下文，延迟跳转实现超时自动休眠。

```c
#include "elib_fsm_cb.h"

enum { PWR_OFF, PWR_STANDBY, PWR_RUNNING, PWR_SLEEP };

typedef struct {
    uint32_t idle_counter;
} power_ctx_t;

static void pwr_entry(elib_fsm_state_t state, void *user_data) {
    power_ctx_t *pc = (power_ctx_t *)user_data;
    switch (state) {
        case PWR_OFF:      set_power_reg(0x00); break;
        case PWR_STANDBY:  set_power_reg(0x10); pc->idle_counter = 0; break;
        case PWR_RUNNING:  set_power_reg(0xFF); pc->idle_counter = 0; break;
        case PWR_SLEEP:    set_power_reg(0x01); break;
    }
}

static void pwr_run(void *user_data) {
    power_ctx_t *pc = (power_ctx_t *)user_data;
    pc->idle_counter++;
    if (pc->idle_counter > IDLE_THRESHOLD) {
        /* 延迟 100ms 后进入休眠，exit(RUNNING) 立即执行 */
        elib_fsm_cb_goto(&pwr_fsm, PWR_SLEEP, 100);
    }
}

static const elib_fsm_cb_state_desc_t pwr_states[] = {
    { PWR_OFF,      pwr_entry, NULL, NULL,    NULL },
    { PWR_STANDBY,  pwr_entry, NULL, pwr_run, NULL },
    { PWR_RUNNING,  pwr_entry, NULL, pwr_run, NULL },
    { PWR_SLEEP,    pwr_entry, NULL, NULL,    NULL },
};

power_ctx_t power_data;
elib_fsm_cb_ctx_t pwr_fsm;
elib_fsm_cb_init(&pwr_fsm, pwr_states, 4, PWR_OFF, &power_data);
```

### 媒体播放器（层次状态机）

典型 HSM 场景：复合状态包含子状态，事件沿层次冒泡处理。

```
           ROOT
          /    \
      STOPPED  RUNNING
               /    \
           PLAYING  PAUSED
```

```c
#include "elib_fsm_hsm.h"

enum {
    ST_ROOT, ST_STOPPED, ST_RUNNING,
    ST_PLAYING, ST_PAUSED
};

enum {
    EVT_CMD_START, EVT_CMD_STOP, EVT_CMD_PAUSE, EVT_CMD_RESUME
};

typedef struct {
    uint32_t play_tick;
} player_data_t;

static void player_entry(elib_fsm_state_t state, void *user_data) {
    player_data_t *pd = (player_data_t *)user_data;
    switch (state) {
        case ST_STOPPED: audio_power_off(); break;
        case ST_RUNNING: audio_power_on();  break;
        case ST_PLAYING: audio_play(); pd->play_tick = 0; break;
        case ST_PAUSED:  audio_pause(); break;
        default: break;
    }
}

static void player_exit(elib_fsm_state_t state, void *user_data) {
    (void)user_data;
    switch (state) {
        case ST_STOPPED: break;
        case ST_RUNNING: audio_power_off(); break;
        case ST_PLAYING: audio_stop(); break;
        case ST_PAUSED:  break;
        default: break;
    }
}

static void playing_run(void *user_data) {
    player_data_t *pd = (player_data_t *)user_data;
    pd->play_tick++;
    audio_feed_decoder();
}

typedef struct {
    int cmd;
} player_event_t;

static bool stopped_handler(void *event_data, void *ud) {
    player_event_t *evt = (player_event_t *)event_data;
    if (evt->cmd == EVT_CMD_START) { elib_fsm_hsm_goto((elib_fsm_hsm_ctx_t*)ud, ST_RUNNING); return true; }
    return false;
}

static bool running_handler(void *event_data, void *ud) {
    player_event_t *evt = (player_event_t *)event_data;
    if (evt->cmd == EVT_CMD_STOP) { elib_fsm_hsm_goto((elib_fsm_hsm_ctx_t*)ud, ST_STOPPED); return true; }
    return false;
}

static bool playing_handler(void *event_data, void *ud) {
    player_event_t *evt = (player_event_t *)event_data;
    if (evt->cmd == EVT_CMD_PAUSE) { elib_fsm_hsm_goto((elib_fsm_hsm_ctx_t*)ud, ST_PAUSED); return true; }
    return false;  /* EVT_CMD_STOP 未处理，冒泡到 RUNNING */
}

static bool paused_handler(void *event_data, void *ud) {
    player_event_t *evt = (player_event_t *)event_data;
    if (evt->cmd == EVT_CMD_RESUME) { elib_fsm_hsm_goto((elib_fsm_hsm_ctx_t*)ud, ST_PLAYING); return true; }
    return false;  /* EVT_CMD_STOP 未处理，冒泡到 RUNNING */
}

static const elib_fsm_hsm_state_desc_t player_states[] = {
    /* state      parent              initial    entry        exit        run         handler          */
    { ST_ROOT,    ELIB_FSM_STATE_INVALID, ST_STOPPED, NULL,        NULL,       NULL,       NULL             },
    { ST_STOPPED, ST_ROOT,   ELIB_FSM_STATE_INVALID, player_entry, player_exit, NULL,       stopped_handler  },
    { ST_RUNNING, ST_ROOT,   ST_PLAYING,  player_entry, player_exit, NULL,       running_handler  },
    { ST_PLAYING, ST_RUNNING, ELIB_FSM_STATE_INVALID, player_entry, player_exit, playing_run, playing_handler  },
    { ST_PAUSED,  ST_RUNNING, ELIB_FSM_STATE_INVALID, player_entry, player_exit, NULL,       paused_handler   },
};

player_data_t player_data;
elib_fsm_hsm_ctx_t player_fsm;
elib_fsm_hsm_init(&player_fsm, player_states, 5, ST_ROOT, &player_data);

/* 初始状态：ROOT -> STOPPED (via initial 链) */
/* entry(ROOT), entry(STOPPED) */

player_event_t evt = {0};

evt.cmd = EVT_CMD_START;
elib_fsm_hsm_dispatch(&player_fsm, &evt);
/* STOPPED 处理 EVT_CMD_START: goto RUNNING */
/* exit(STOPPED), entry(RUNNING), entry(PLAYING via initial) */

evt.cmd = EVT_CMD_PAUSE; elib_fsm_hsm_dispatch(&player_fsm, &evt);
/* PLAYING 处理 EVT_CMD_PAUSE: goto PAUSED */
/* exit(PLAYING), entry(PAUSED) */

evt.cmd = EVT_CMD_STOP; elib_fsm_hsm_dispatch(&player_fsm, &evt);
/* PAUSED 不处理 EVT_CMD_STOP -> 冒泡到 RUNNING */
/* RUNNING 处理 EVT_CMD_STOP: goto STOPPED */
/* exit(PAUSED), exit(RUNNING), entry(STOPPED) */

/* 主循环中驱动 tick */
while (1) {
    elib_fsm_hsm_poll(&player_fsm);  /* 调用当前叶子状态的 run 回调 */
}
```

## API 参考

### Switch/Case 状态机

| 函数 | 说明 |
|------|------|
| `elib_fsm_init(ctx, initial)` | 初始化，设置初始状态 |
| `elib_fsm_deinit(ctx)` | 反初始化 |
| `elib_fsm_goto(ctx, target, delay_ms)` | 跳转状态，delay=0 立即跳转，delay>0 延迟跳转 |
| `elib_fsm_poll(ctx, period_ms)` | 推进一个 tick，返回当前状态；延迟等待中返回 -1 |
| `elib_fsm_current(ctx)` | 获取当前状态 |

### 回调状态机

| 函数 | 说明 |
|------|------|
| `elib_fsm_cb_init(ctx, states, state_count, initial, user_data)` | 初始化，设置状态描述符和初始状态 |
| `elib_fsm_cb_deinit(ctx)` | 反初始化 |
| `elib_fsm_cb_goto(ctx, target, delay_ms)` | 跳转状态，delay=0 立即跳转(exit→entry)，delay>0 延迟跳转(exit 立即，entry 延迟) |
| `elib_fsm_cb_poll(ctx, period_ms, event_data)` | 推进一个 tick，返回当前状态；无延迟时先调用 event 回调再调用 run 回调；延迟等待中跳过 event 和 run，返回 -1；到期时执行 entry，再调用新状态的 event 和 run |
| `elib_fsm_cb_dispatch(ctx, event_data)` | 按需投递事件，仅调用当前状态的 event 回调（不触发 run）；延迟等待中跳过；可在一个 tick 内多次调用 |
| `elib_fsm_cb_current(ctx)` | 获取当前状态 |
| `elib_fsm_cb_previous(ctx)` | 获取上一个有效状态（延迟跳转期间不返回 INVALID） |

### 层次状态机

**事件系统：**

使用 `void *event_data`，用户自定义事件类型，handler 接收 `void *` 并自行转型。

**状态描述符** (`elib_fsm_hsm_state_desc_t`)：

| 字段 | 类型 | 说明 |
|------|------|------|
| `state` | `int` | 状态值 |
| `parent` | `int` | 父状态，INVALID = 顶层 |
| `initial` | `int` | 默认子状态，叶子状态必须为 INVALID |
| `entry` | `callback` | 进入回调（nullable） |
| `exit` | `callback` | 退出回调（nullable） |
| `run` | `callback` | 周期回调（nullable） |
| `handler` | `callback` | 事件处理回调（nullable），签名：`bool (*)(void *event_data, void *user_data)` |

**`initial` 字段规则：**

| 状态类型 | `initial` 值 | 说明 |
|---------|-------------|------|
| 复合状态 | 必须有效 | 指定默认子状态，`goto` 时沿此链下降 |
| 叶子状态 | 必须为 INVALID | 无子状态 |

**API：**

| 函数 | 说明 |
|------|------|
| `elib_fsm_hsm_init(ctx, states, state_count, initial, user_data)` | 初始化，沿 initial 链下降到叶子状态，调用路径上所有 entry 回调 |
| `elib_fsm_hsm_deinit(ctx)` | 反初始化 |
| `elib_fsm_hsm_goto(ctx, target)` | 跳转状态（LCA 语义），exit 从叶子到 LCA，entry 从 LCA 到目标，composite 状态沿 initial 链下降 |
| `elib_fsm_hsm_poll(ctx)` | 推进一个 tick，调用叶子状态 run 回调，返回当前叶子状态 |
| `elib_fsm_hsm_dispatch(ctx, event_data)` | 投递事件（`void *`），从叶子状态沿活跃路径冒泡，返回是否被处理 |
| `elib_fsm_hsm_current(ctx)` | 获取当前叶子状态 |

### 带历史的层次状态机

**状态描述符** (`elib_fsm_hsm_hist_state_desc_t`)：

| 字段 | 类型 | 说明 |
|------|------|------|
| `state` | `int` | 状态值 |
| `parent` | `int` | 父状态，INVALID = 顶层 |
| `initial` | `int` | 默认子状态，叶子状态必须为 INVALID |
| `last_active` | `int` | 运行时追踪：最近活跃的子状态（框架自动管理，用户不加 const） |
| `entry` | `callback` | 进入回调（nullable） |
| `exit` | `callback` | 退出回调（nullable） |
| `run` | `callback` | 周期回调（nullable） |
| `handler` | `callback` | 事件处理回调（nullable），签名：`bool (*)(void *event_data, void *user_data)` |

**`initial` 字段规则：**

| 状态类型 | `initial` 值 | 说明 |
|---------|-------------|------|
| 复合状态 | 必须有效 | 指定默认子状态，`goto` 时沿此链下降 |
| 叶子状态 | 必须为 INVALID | 无子状态 |

**API：**

| 函数 | 说明 |
|------|------|
| `elib_fsm_hsm_hist_init(ctx, states, state_count, initial, user_data)` | 初始化，自动将所有 `last_active = initial`，沿 initial 链下降到叶子 |
| `elib_fsm_hsm_hist_deinit(ctx)` | 反初始化 |
| `elib_fsm_hsm_hist_goto(ctx, target)` | 跳转状态，使用 `last_active` 链找到目标叶子（恢复历史），无历史时走 initial 链 |
| `elib_fsm_hsm_hist_reset(ctx)` | 重置：恢复所有 `last_active = initial`，回到初始叶子 |
| `elib_fsm_hsm_hist_poll(ctx)` | 推进一个 tick，调用叶子状态 run 回调 |
| `elib_fsm_hsm_hist_dispatch(ctx, event_data)` | 投递事件（`void *`），从叶子状态沿活跃路径冒泡 |
| `elib_fsm_hsm_hist_current(ctx)` | 获取当前叶子状态 |

### 标准 HSM vs 带历史 HSM 对比

| 方面 | `elib_fsm_hsm` | `elib_fsm_hsm_hist` |
|------|---------------|---------------------|
| **状态描述符** | 7 字段（const，可放 Flash） | 8 字段（非 const，含 `last_active`，只能放 RAM） |
| **goto 行为** | 始终沿 initial 链下降到叶子 | 优先恢复 `last_active`（历史），无历史走 initial 链 |
| **reset** | 无 | 恢复所有 `last_active = initial` |
| **`initial` 字段** | 复合状态必须有效 | 复合状态必须有效 |
| **`last_active` 字段** | 无 | 框架自动管理，用户不应修改 |
| **适用场景** | 简单层次结构，每次进入都从初始状态开始 | 需要记住离开时的位置，恢复上下文 |

**使用场景对比：**

```
场景：ROOT → A → A2 → goto(B) → goto(A)

标准 HSM:
  goto(A) → 总是走 initial → A1

带历史 HSM:
  goto(A) → 恢复历史 → A2
```

**选择建议：**

- 使用 `elib_fsm_hsm`：状态机每次进入复合状态都从固定初始状态开始（如通信协议、简单 UI）
- 使用 `elib_fsm_hsm_hist`：需要记住离开时的位置，返回时恢复（如复杂 UI 页面、多任务上下文）

## 编译

```bash
# Switch/Case 状态机
gcc -c elib-state-machine/src/elib_fsm_core.c -I elib-state-machine/include

# 回调状态机
gcc -c elib-state-machine/src/elib_fsm_cb_core.c -I elib-state-machine/include

# 层次状态机
gcc -c elib-state-machine/src/elib_fsm_hsm_core.c -I elib-state-machine/include

# 带历史的层次状态机
gcc -c elib-state-machine/src/elib_fsm_hsm_hist_core.c -I elib-state-machine/include
```

## 测试

```bash
gcc -o test_elib_fsm elib-state-machine/test/test_elib_fsm.c \
    elib-state-machine/src/elib_fsm_core.c -I elib-state-machine/include && ./test_elib_fsm

gcc -o test_elib_fsm_cb elib-state-machine/test/test_elib_fsm_cb.c \
    elib-state-machine/src/elib_fsm_cb_core.c -I elib-state-machine/include && ./test_elib_fsm_cb

gcc -o test_elib_fsm_hsm elib-state-machine/test/test_elib_fsm_hsm.c \
    elib-state-machine/src/elib_fsm_hsm_core.c -I elib-state-machine/include && ./test_elib_fsm_hsm

gcc -o test_elib_fsm_hsm_hist elib-state-machine/test/test_elib_fsm_hsm_hist.c \
    elib-state-machine/src/elib_fsm_hsm_hist_core.c -I elib-state-machine/include && ./test_elib_fsm_hsm_hist
```
