---
AIGC:
    Label: "1"
    ContentProducer: 001191440300708461136T1XGW3
    ProduceID: 38fb6e2bf05359cb484e45e4f826fb7c_dc9e410166f311f1a99c5254007bceed
    ReservedCode1: VuIVuNekxv7KUkLs3Isz04PnU9cIXd7heaywKiTbhwF9g0vTT5iv+fa6nMaZXNYwmsDZZerpMoNFRAX3737GqOFgAs17hMG9q4ZoButNAWqVSERez9Ng4DGeeMKO4W0wR7hzNV3Q18c3p3+iWxDRV0uIWgm2ykt35u6vdcqDyfivewEUHTYErBd7X7M=
    ContentPropagator: 001191440300708461136T1XGW3
    PropagateID: 38fb6e2bf05359cb484e45e4f826fb7c_dc9e410166f311f1a99c5254007bceed
    ReservedCode2: VuIVuNekxv7KUkLs3Isz04PnU9cIXd7heaywKiTbhwF9g0vTT5iv+fa6nMaZXNYwmsDZZerpMoNFRAX3737GqOFgAs17hMG9q4ZoButNAWqVSERez9Ng4DGeeMKO4W0wR7hzNV3Q18c3p3+iWxDRV0uIWgm2ykt35u6vdcqDyfivewEUHTYErBd7X7M=
---

# ZW101 指纹模块 C 语言驱动及示例代码

> **版本**: 1.0.0 | **日期**: 2026-06-13 | **平台**: 嵌入式 C (C99)

## 1. 概述

本项目是 ZW101 半导体指纹处理模块的完整 C 语言驱动及示例程序，基于官方 `fp_syno_protocol.c/h` 协议栈和《指纹模组产品用户手册 V1.5.1》开发，应用层代码风格参考 `bq25710_app.c/h` 的分层状态机模式。

### ZW101 模块关键参数

| 参数 | 规格 |
|------|------|
| 传感器类型 | 一体化半导体指纹模组 |
| 分辨率 | 500 DPI |
| 像素 | 80×64 |
| 指纹容量 | 50 枚 |
| 比对速度 | < 0.8s |
| 通信接口 | UART (默认 57600bps, 8N1) |
| 供电电压 | V_SENSOR: 3.3V, VCC: 3.3V |
| 工作电流 | 35~45mA |
| 静态功耗 | 8~12μA |

## 2. 文件结构

```
output/
├── zw101_fp_protocol.h      # 协议层头文件: 数据包定义、指令码、确认码、RGB枚举
├── zw101_fp_protocol.c      # 协议层实现: 校验和计算、组包、解包、错误码描述
├── zw101_fp_driver.h        # 驱动层头文件: HAL接口、操作API、事件回调
├── zw101_fp_driver.c        # 驱动层实现: 录入/验证/删除/LED/休眠状态机
├── zw101_fp_app.h           # 应用层头文件: 状态机枚举、上下文结构、公开接口
├── zw101_fp_app.c           # 应用层实现: 初始化/自检/demo演示状态机
└── README.md                # 本文件
```

### 架构分层

```
┌─────────────────────────────────────────────┐
│  应用层 (zw101_fp_app.c/h)                    │
│  ─ 状态机驱动的演示流程 (参考 bq25710 风格)      │
│  ─ INIT → SELF_TEST → ENROLL/VERIFY/DELETE   │
│  ─ 子步骤管理、超时监控、错误恢复                │
├─────────────────────────────────────────────┤
│  驱动层 (zw101_fp_driver.c/h)                 │
│  ─ 高层操作封装                               │
│  ─ 录入: 多帧采集 → 生成特征 → 合并 → 存储     │
│  ─ 验证: 采集 → 生成特征 → 1:N搜索/1:1比对    │
│  ─ 删除: 按ID删除 / 全部清空                   │
│  ─ LED/RGB控制、蜂鸣器、休眠/唤醒              │
├─────────────────────────────────────────────┤
│  协议层 (zw101_fp_protocol.c/h)                │
│  ─ 纯数据运算，无平台依赖                       │
│  ─ 校验和、组包、解包、错误码枚举               │
├─────────────────────────────────────────────┤
│  HAL 层 (用户自行实现)                          │
│  ─ uart_send / uart_recv (超时接收)            │
│  ─ delay_ms / get_tick_ms (时间基准)           │
│  ─ power_ctrl / read_touch_out / buzzer_ctrl   │
└─────────────────────────────────────────────┘
```

## 3. 快速入门

### 3.1 步骤 1: 实现 HAL 回调

根据你的 MCU 平台实现 `zw101_hal_t` 中的所有回调函数。

**CH32x035 示例 (UART2)**:

```c
#include "zw101_fp_driver.h"
#include "ch32x035_usart.h"
#include "ch32x035_gpio.h"

static void my_uart_send(const uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++) {
        USART_SendData(USART2, data[i]);
        while (USART_GetFlagStatus(USART2, USART_FLAG_TC) == RESET);
    }
}

static uint16_t my_uart_recv(uint8_t *buf, uint16_t max_len, uint32_t timeout_ms)
{
    uint32_t start = SysTick_GetTick();
    uint16_t count = 0;
    while (count < max_len) {
        if (USART_GetFlagStatus(USART2, USART_FLAG_RXNE) != RESET) {
            buf[count++] = USART_ReceiveData(USART2);
        }
        if ((SysTick_GetTick() - start) >= timeout_ms) break;
    }
    return count;
}

static void my_delay_ms(uint32_t ms) { Delay_Ms(ms); }
static uint32_t my_get_tick_ms(void) { return SysTick_GetTick(); }

static void my_power_ctrl(bool enable)
{
    GPIO_WriteBit(GPIOA, GPIO_Pin_0, enable ? Bit_SET : Bit_RESET);
}

static uint8_t my_read_touch_out(void)
{
    return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_1);
}

static void my_buzzer_ctrl(uint16_t duration_ms, uint16_t freq_hz)
{
    (void)freq_hz;
    BEEP_On();
    Delay_Ms(duration_ms);
    BEEP_Off();
}

/* HAL 实例 */
static const zw101_hal_t g_hal = {
    .uart_send       = my_uart_send,
    .uart_recv       = my_uart_recv,
    .delay_ms        = my_delay_ms,
    .get_tick_ms     = my_get_tick_ms,
    .power_ctrl      = my_power_ctrl,
    .read_touch_out  = my_read_touch_out,
    .buzzer_ctrl     = my_buzzer_ctrl,
    .enter_critical  = NULL,  /* 单任务环境可为 NULL */
    .exit_critical   = NULL,
};
```

### 3.2 步骤 2: 初始化

```c
zw101_app_ctx_t g_app_ctx;

int main(void)
{
    SystemInit();

    /* 初始化应用层 (自动初始化驱动层) */
    zw101_drv_err_t ret = zw101_app_init(&g_app_ctx, &g_hal, NULL);
    if (ret != ZW101_DRV_OK) {
        /* 初始化失败, 检查 UART/GPIO/电源 */
        while (1);
    }

    while (1) {
        zw101_app_process(&g_app_ctx);

        /* 用户可通过按键/串口命令启动演示 */
        if (g_key_enroll_pressed) {
            zw101_app_start_demo(&g_app_ctx, ZW101_DEMO_ENROLL);
        }
        if (g_key_verify_pressed) {
            zw101_app_start_demo(&g_app_ctx, ZW101_DEMO_VERIFY);
        }
    }
}
```

### 3.3 步骤 3: 运行演示

自检通过后，应用层进入 `IDLE` 状态。调用 `zw101_app_start_demo()` 启动指定演示。

## 4. API 参考

### 4.1 协议层 (`zw101_fp_protocol.h`)

| 函数 | 说明 |
|------|------|
| `zw101_calc_checksum()` | 计算协议校验和 (16位, 从 pkg_type 累加到 checksum 前) |
| `zw101_build_cmd_pkg()` | 构建完整的 ZW101 命令包 |
| `zw101_parse_sys_para()` | 从应答包解析模组基本参数 (16字节) |
| `zw101_parse_valid_count()` | 解析有效模板个数 |
| `zw101_parse_search_result()` | 解析 1:N 搜索结果 (PageID + Score) |
| `zw101_ack_to_string()` | 确认码转英文描述 |
| `zw101_err_to_string()` | 确认码转中文描述 |

### 4.2 驱动层 (`zw101_fp_driver.h`)

#### 初始化与自检

| 函数 | 说明 |
|------|------|
| `zw101_driver_init()` | 注册 HAL 回调 |
| `zw101_power_on()` | 上电并等待握手信号 0x55 |
| `zw101_power_off()` | 断电 |
| `zw101_self_test()` | 握手 → 读参数 → 校验传感器 → 读模板数 |

#### 指纹录入

| 函数 | 说明 |
|------|------|
| `zw101_enroll_start()` | 开始异步注册 (需轮询 process) |
| `zw101_enroll_process()` | 推进注册状态机 (主循环中调用) |
| `zw101_enroll_cancel()` | 取消注册 |
| `zw101_enroll_get_progress()` | 获取注册进度 |

#### 指纹验证

| 函数 | 说明 |
|------|------|
| `zw101_verify_start()` | 开始 1:N 搜索验证 |
| `zw101_identify_start()` | 开始 1:1 指定 ID 比对 |
| `zw101_verify_process()` | 推进验证状态机 |
| `zw101_verify_cancel()` | 取消验证 |

#### 指纹管理

| 函数 | 说明 |
|------|------|
| `zw101_delete_finger()` | 删除指定 ID 指纹 |
| `zw101_delete_all()` | 清空指纹库 |
| `zw101_get_stored_count()` | 获取已存储指纹数 |
| `zw101_get_index_table()` | 获取 32 字节索引表 (位图) |
| `zw101_is_id_occupied()` | 查询指定 ID 是否已录入 |
| `zw101_find_free_id()` | 查找第一个空闲 ID |

#### LED / 蜂鸣器

| 函数 | 说明 |
|------|------|
| `zw101_rgb_ctrl()` | 通用 RGB 控制 (功能码+颜色+占空比+循环) |
| `zw101_rgb_standby()` | 蓝色呼吸灯 (待机) |
| `zw101_rgb_success()` | 绿色闪烁 (成功) |
| `zw101_rgb_failure()` | 红色闪烁 (失败) |
| `zw101_rgb_off()` | 关闭 LED |
| `zw101_beep()` | 蜂鸣器提示 |

#### 休眠

| 函数 | 说明 |
|------|------|
| `zw101_enter_sleep()` | 进入低功耗模式 |
| `zw101_wakeup()` | 重新上电唤醒 |

### 4.3 应用层 (`zw101_fp_app.h`)

| 函数 | 说明 |
|------|------|
| `zw101_app_init()` | 初始化应用层 (自动执行 INIT → SELF_TEST) |
| `zw101_app_process()` | 主循环 (驱动状态机推进) |
| `zw101_app_start_demo()` | 启动演示 (ENROLL/VERIFY/DELETE/DELETE_ALL) |
| `zw101_app_stop_demo()` | 停止演示 |
| `zw101_app_state_name()` | 状态枚举转字符串 (调试用) |
| `zw101_app_version()` | 版本号 |

## 5. 通信协议要点

### 5.1 数据包结构

```
命令包: EF 01 | DevAddr(4B) | 01 | Len(2B) | Cmd(1B) | [Params] | Check(2B)
应答包: EF 01 | DevAddr(4B) | 07 | Len(2B) | Ack(1B) | [RetData] | Check(2B)
数据包: EF 01 | DevAddr(4B) | 02/08 | Len(2B) | Data | Check(2B)
```

### 5.2 校验和

累加范围从 pkg_type (偏移6) 到 checksum 前一个字节，取低 16 位。

### 5.3 关键指令列表

| 指令码 | 名称 | 说明 |
|--------|------|------|
| 0x01 | PS_GetImage | 验证用获取图像 |
| 0x02 | PS_GenChar | 从图像缓冲区生成特征 |
| 0x04 | PS_Search | 1:N 搜索指纹库 |
| 0x05 | PS_RegModel | 合并特征生成模板 |
| 0x06 | PS_StoreChar | 存储模板到指纹库 |
| 0x0C | PS_DeletChar | 删除指定模板 |
| 0x0D | PS_Empty | 清空指纹库 |
| 0x0F | PS_ReadSysPara | 读模组基本参数 |
| 0x1D | PS_ValidTempleteNum | 读有效模板个数 |
| 0x29 | PS_GetEnrollImage | 注册用获取图像 |
| 0x33 | PS_Sleep | 进入休眠 |
| 0x35 | PS_HandShake | 握手指令 |
| 0x36 | PS_CheckSensor | 校验传感器 |
| 0x3C | PS_RGBLED | RGB 灯控 |

## 6. 状态机流程

### 6.1 应用层状态机

```
INIT ──→ SELF_TEST ──→ IDLE ──→ ENROLL_DEMO ──→ IDLE
                         │         │
                         │         ├──→ VERIFY_DEMO ──→ IDLE
                         │         │
                         │         └──→ DELETE_DEMO ──→ IDLE
                         │
                         ├──→ TIMEOUT ──→ IDLE
                         └──→ ERROR ──→ (恢复) ──→ IDLE
```

### 6.2 注册驱动状态机

```
WAIT_FINGER ─→ CAPTURING ─→ EXTRACTING ─→ WAIT_RELEASE
     ↑                                        │
     └──────────────────── (循环 N 次) ────────┘
                                                │
                                          MERGING ─→ STORING ─→ DONE
```

### 6.3 验证驱动状态机

```
WAIT_FINGER ─→ CAPTURING ─→ EXTRACTING ─→ SEARCHING ─→ DONE/FAILED
```

## 7. 错误码速查

| 确认码 | 含义 | 驱动错误码 |
|--------|------|-----------|
| 0x00 | 成功 | ZW101_DRV_OK |
| 0x01 | 收包错误 | ZW101_DRV_ERR_PROTO / ZW101_DRV_ERR_CHECKSUM |
| 0x02 | 无手指 | ZW101_DRV_ERR_NO_FINGER |
| 0x03 | 录入图像失败 | ZW101_DRV_ERR_MODULE |
| 0x04-0x07 | 图像质量问题 | ZW101_DRV_ERR_MODULE |
| 0x08 | 指纹不匹配 | ZW101_DRV_ERR_NOT_MATCH |
| 0x09 | 未搜索到 | ZW101_DRV_ERR_NOT_MATCH |
| 0x0B | ID 超出范围 | ZW101_DRV_ERR_PARAM |
| 0x1F | 指纹库已满 | ZW101_DRV_ERR_LIB_FULL |
| 0x27 | 指纹已存在 | ZW101_DRV_ERR_DUPLICATE |
| 0x26 | 超时 | ZW101_DRV_ERR_TIMEOUT |

## 8. 平台适配注意事项

1. **UART 时序**: ZW101 默认波特率 57600bps, 8N1，需确保 MCU 时钟精度足够
2. **电平匹配**: ZW101 为 3.3V 电平，若 MCU 为 5V 需加电平转换
3. **电源管理**: V_SENSOR 和 VCC 可独立供电以实现更低功耗
4. **TOUCH_OUT**: 为输出信号，低有效，可接 MCU 外部中断唤醒
5. **休眠唤醒**: 休眠时 TOUCH_OUT 仍有效，可唤醒 MCU
6. **临界区**: RTOS 环境下建议实现 enter_critical/exit_critical 保护 UART 收发

## 9. 参考文件

- `fp_syno_protocol.c/h` — 官方 UART 协议栈示例 (SynoChip SDK)
- `bq25710_app.c/h` — 应用层状态机参考架构
- 《指纹模组产品用户手册 V1.5.1》 — 完整指令集与协议
- 《ZW101 半导体指纹处理模块规格书 V1.2》 — 硬件参数
*（内容由AI生成，仅供参考）*
