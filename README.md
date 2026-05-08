# elib-state-machine

嵌入式应用有限状态机框架。

## 特性

- **Switch/Case 状态机** (`elib_fsm`): 轻量级状态跟踪器，支持立即/延迟跳转
- **回调状态机** (`elib_fsm_cb`): 回调驱动型状态机，支持 entry/exit/run 回调调度与延迟跳转
- 零动态内存分配
- 用户分配上下文
- 两种模式完全解耦，可独立或组合使用

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

static const elib_fsm_cb_state_desc_t states[] = {
    { STATE_IDLE,   on_entry, on_exit, on_run },
    { STATE_ACTIVE, on_entry, on_exit, on_run },
};

elib_fsm_cb_ctx_t ctx;
elib_fsm_cb_init(&ctx, states, 2, STATE_IDLE, NULL);

elib_fsm_cb_goto(&ctx, STATE_ACTIVE, 0);  /* exit(IDLE) -> entry(ACTIVE) */
elib_fsm_cb_poll(&ctx, 10);              /* 10ms tick，自动调用 on_run */
```

## 使用案例

### 按键消抖（Switch/Case 状态机）

最简单的状态跟踪场景：延迟跳转天然支持消抖定时。

```c
#include "elib_fsm.h"

enum { KEY_STABLE, KEY_PRESSED, KEY_DEBOUNCE };

elib_fsm_ctx_t key_fsm;
elib_fsm_init(&key_fsm, KEY_STABLE);

/* 主循环中根据状态执行逻辑 */
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
    { PROTO_IDLE,        proto_entry, proto_exit, NULL      },
    { PROTO_WAIT_HEADER, proto_entry, proto_exit, NULL      },
    { PROTO_RECV_DATA,   proto_entry, proto_exit, proto_run },
    { PROTO_CHECKSUM,    proto_entry, proto_exit, NULL      },
};

proto_data_t proto_data;
elib_fsm_cb_ctx_t proto_fsm;
elib_fsm_cb_init(&proto_fsm, proto_states, 4, PROTO_IDLE, &proto_data);

/* 中断中直接跳转状态 */
elib_fsm_cb_goto(&proto_fsm, PROTO_WAIT_HEADER, 0);

/* 主循环中驱动 */
while (1) {
    elib_fsm_cb_poll(&proto_fsm, 10);  /* 10ms tick，自动调用 run 回调 */
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
    { PWR_OFF,      pwr_entry, NULL, NULL    },
    { PWR_STANDBY,  pwr_entry, NULL, pwr_run },
    { PWR_RUNNING,  pwr_entry, NULL, pwr_run },
    { PWR_SLEEP,    pwr_entry, NULL, NULL    },
};

power_ctx_t power_data;
elib_fsm_cb_ctx_t pwr_fsm;
elib_fsm_cb_init(&pwr_fsm, pwr_states, 4, PWR_OFF, &power_data);
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
| `elib_fsm_cb_poll(ctx, period_ms)` | 推进一个 tick，返回当前状态；延迟等待中返回 -1；到期时执行 entry；自动调用当前状态 run 回调 |
| `elib_fsm_cb_current(ctx)` | 获取当前状态 |

## 编译

```bash
# Switch/Case 状态机
gcc -c elib-state-machine/src/elib_fsm_core.c -I elib-state-machine/include

# 回调状态机
gcc -c elib-state-machine/src/elib_fsm_cb_core.c -I elib-state-machine/include
```

## 测试

```bash
gcc -o test_elib_fsm elib-state-machine/test/test_elib_fsm.c \
    elib-state-machine/src/elib_fsm_core.c -I elib-state-machine/include && ./test_elib_fsm

gcc -o test_elib_fsm_cb elib-state-machine/test/test_elib_fsm_cb.c \
    elib-state-machine/src/elib_fsm_cb_core.c -I elib-state-machine/include && ./test_elib_fsm_cb
```
