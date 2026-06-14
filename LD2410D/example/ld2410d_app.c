/**
 * @file ld2410d_app.c
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief LD2410D 人体存在雷达应用示例 — 初始化、配置、周期性读取目标状态
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @note  硬件引脚定义
 *    LD2410D_OUT          PA4  (输入, 可选 — 高电平=有人, 低电平=无人)
 *    LD2410D_UART_TX      PA3  (MCU USART2 RX)
 *    LD2410D_UART_RX      PA2  (MCU USART2 TX)
 *
 * @note  通信规格
 *    波特率: 115200, 1 停止位, 无校验, 5V 供电
 *
 * @note  使用前需实现并注册 ld2410d.dev.uart 操作集 (send / recv / flush)
 *        参考下方 uart_send / uart_recv / uart_flush 模板实现
 */

#include "ld2410d_app.h"
#include <string.h>
#include <stdio.h>

/* ---------- 以下头文件为示例, 请替换为实际 MCU HAL ---------- */
// #include "ch32x035.h"
// #include "usart.h"
// #include "gpio.h"
// #include "timer.h"

#define LOG_LEVEL LOG_LVL_DEBUG
#define LOG_TAG "LD2410D APP"
// #include "logger.h"

/* 无 logger 时的简易宏 (实际使用时替换为 logger.h) */
#ifndef LOG_I
#define LOG_I(fmt, ...)  printf("[INFO ] " fmt "\r\n", ##__VA_ARGS__)
#endif
#ifndef LOG_E
#define LOG_E(fmt, ...)  printf("[ERROR] " fmt "\r\n", ##__VA_ARGS__)
#endif
#ifndef LOG_D
#define LOG_D(fmt, ...)  printf("[DEBUG] " fmt "\r\n", ##__VA_ARGS__)
#endif

/* ==================== UART 操作集模板 (平台实现) ==================== */

/*
 * 以下为 UART 操作集模板, 实际使用时请根据 MCU 平台实现并注册到 ld2410d.dev.uart:
 *
 *   void my_uart_send (const uint8_t *data, uint16_t len) {
 *       USART_DMA_Send (USART2, data, len);
 *       while (USART_GetFlagStatus (USART2, USART_FLAG_TC) == RESET);
 *   }
 *
 *   uint16_t my_uart_recv (uint8_t *buf, uint16_t max_len, uint32_t timeout_ms) {
 *       return USART_Recv_Timeout (USART2, buf, max_len, timeout_ms);
 *   }
 *
 *   void my_uart_flush (void) {
 *       USART_Flush_RX (USART2);
 *   }
 *
 * 注册方式:
 *   ld2410d.dev.uart.send  = my_uart_send;
 *   ld2410d.dev.uart.recv  = my_uart_recv;
 *   ld2410d.dev.uart.flush = my_uart_flush;
 */

/* ==================== 全局应用实例 ==================== */

struct ld2410d_app_t ld2410d;

/* ==================== 状态机名称表 (调试用) ==================== */

static const char *const ld2410d_machine_state_names[] = {
    "LD2410D_STATE_INIT",
    "LD2410D_STATE_CHECK",
    "LD2410D_STATE_CONFIGURE",
    "LD2410D_STATE_OPERATION",
    "LD2410D_STATE_IDLE",
    "LD2410D_STATE_TIMEOUT",
    "LD2410D_STATE_ERROR",
};

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief 获取当前毫秒时间戳
 *
 * 需根据 MCU 平台实现, 例如使用 SysTick 或定时器
 */
static uint64_t get_time_ms (void)
{
    /* TODO: 替换为实际时间获取函数, 如 get_time().val */
    // union timestamp_u t = get_time ();
    // return t.val;
    return 0;
}

static void set_state_machine_timeout (uint64_t timeout,
                                        enum ld2410d_app_states_e timeout_state)
{
    ld2410d.timeout       = timeout;
    ld2410d.timeout_state = timeout_state;
}

/**
 * @brief 切换状态机状态, 自动记录耗时
 */
static void set_state_machine_state (enum ld2410d_app_states_e next_state)
{
    enum ld2410d_app_states_e last_state = ld2410d.task_state;

    if (last_state == next_state) {
        return;
    }

    /* 记录上一阶段的耗时 */
    if (last_state < LD2410D_STATE_COUNT) {
        if (ld2410d.stages[last_state].enter_ms != 0) {
            uint64_t now_ms = get_time_ms ();
            ld2410d.stages[last_state].elapsed_ms =
                (uint32_t)(now_ms - ld2410d.stages[last_state].enter_ms);
        }
    }

    /* 清除超时计时器 */
    set_state_machine_timeout (0, 0);

    /* 记录新阶段的进入时间 */
    ld2410d.task_state = next_state;
    if (next_state < LD2410D_STATE_COUNT) {
        ld2410d.stages[next_state].enter_ms  = (uint32_t)get_time_ms ();
        ld2410d.stages[next_state].elapsed_ms = 0;
    }

    ld2410d.sub_step = 0;

    LOG_I ("%s -> %s",
           ld2410d_machine_state_names[last_state],
           ld2410d_machine_state_names[next_state]);
}

/**
 * @brief 设置超时并跳转错误状态
 */
static void enter_error_state (const char *reason)
{
    LOG_E ("%s", reason);
    set_state_machine_state (LD2410D_STATE_ERROR);
}

/* ==================== 目标状态上报 ==================== */

/**
 * @brief 检测状态变化并触发回调
 */
static void check_and_report_target (enum ld2410d_detect_status_e status,
                                      uint16_t distance)
{
    /* 状态或距离变化时触发回调 */
    if (status != ld2410d.last_status || distance != ld2410d.last_distance) {
        ld2410d.last_status   = status;
        ld2410d.last_distance = distance;

        if (status != LD2410D_DETECT_NONE) {
            ld2410d.result.detected = 1;
        }

        if (ld2410d.target_callback) {
            ld2410d.target_callback (status, distance, ld2410d.target_cb_arg);
        }

        /* 日志输出 */
        switch (status) {
        case LD2410D_DETECT_NONE:
            LOG_I ("目标状态: 无人");
            break;
        case LD2410D_DETECT_MOVING:
            LOG_I ("目标状态: 有人 (运动)  距离: %u cm", distance);
            break;
        case LD2410D_DETECT_STATIC:
            LOG_I ("目标状态: 有人 (静止)  距离: %u cm", distance);
            break;
        default:
            break;
        }
    }
}

/* ==================== 各阶段处理函数 ==================== */

/**
 * @brief 阶段 0: 初始化 (INIT)
 *
 * 步骤:
 *   0. GPIO 引脚初始化 (OUT 检测引脚)
 *   1. 调用 ld2410d_init() 初始化设备句柄
 *   2. 注册 UART 操作集
 */
static void process_state_init (void)
{
    ld2410d.sub_step++;

    switch (ld2410d.sub_step) {

    case 1:
        LOG_I ("开始初始化...");

        /*
         * GPIO 初始化: OUT 引脚 (PA4) — 下拉输入
         * 高电平 = 有人, 低电平 = 无人
         *
         * TODO: 替换为实际 GPIO 初始化代码
         *
         * GPIO_InitTypeDef GPIO_InitStructure;
         * GPIO_InitStructure.GPIO_Pin  = GPIO_Pin_4;
         * GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPD;
         * GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
         * GPIO_Init (GPIOA, &GPIO_InitStructure);
         */
        LOG_I ("GPIO OUT 引脚初始化 (PA4, 下拉输入)");
        break;

    case 2:
        /*
         * UART 初始化: PA2(TX) / PA3(RX), 115200-8-N-1
         *
         * TODO: 替换为实际 UART 初始化代码
         *
         * USART_Init (USART2, 115200, USART_WordLength_8b,
         *             USART_StopBits_1, USART_Parity_No);
         */

        /* 注册 UART 操作集 (用户需预先实现 send / recv / flush) */
        // ld2410d.dev.uart.send  = uart2_send;
        // ld2410d.dev.uart.recv  = uart2_recv_timeout;
        // ld2410d.dev.uart.flush = uart2_flush_rx;

        LOG_I ("UART 初始化 (115200-8-N-1)");
        break;

    case 3:
        /* 初始化驱动设备句柄 */
        ld2410d_init (&ld2410d.dev, &ld2410d.dev.uart);
        ld2410d.result.init_ok = 1;
        LOG_I ("LD2410D 驱动初始化成功");
        set_state_machine_state (LD2410D_STATE_CHECK);
        break;

    default:
        break;
    }
}

/**
 * @brief 阶段 1: 设备检测 (CHECK)
 *
 * 步骤:
 *   0. 使能配置模式
 *   1. 读取固件版本
 *   2. 读取序列号
 *   3. 结束配置模式
 */
static void process_state_check (void)
{
    int8_t ret;

    ld2410d.sub_step++;

    switch (ld2410d.sub_step) {

    case 1:
        LOG_I ("使能配置模式...");
        ret = ld2410d_enable_config (&ld2410d.dev);
        if (ret != LD2410D_OK) {
            enter_error_state ("使能配置模式失败");
            return;
        }
        LOG_I ("配置模式已使能");
        break;

    case 2:
        LOG_I ("读取固件版本...");
        {
            uint8_t ver_len = 0;
            ret = ld2410d_read_firmware_version (&ld2410d.dev,
                                                  ld2410d.fw_version,
                                                  &ver_len);
            if (ret != LD2410D_OK) {
                LOG_E ("读取固件版本失败 (ret=%d)", ret);
            } else {
                ld2410d.fw_version[ver_len] = '\0';
                ld2410d.result.version_ok = 1;
                LOG_I ("固件版本: %s", ld2410d.fw_version);
            }
        }
        break;

    case 3:
        LOG_I ("读取序列号...");
        {
            uint8_t sn_len = 0;
            ret = ld2410d_read_sn_char (&ld2410d.dev,
                                         ld2410d.sn_str, &sn_len);
            if (ret != LD2410D_OK) {
                LOG_E ("读取序列号失败 (ret=%d)", ret);
            } else {
                ld2410d.sn_str[sn_len] = '\0';
                ld2410d.result.sn_ok = 1;
                LOG_I ("序列号: %s", ld2410d.sn_str);
            }
        }
        break;

    case 4:
        LOG_I ("结束配置模式...");
        ret = ld2410d_end_config (&ld2410d.dev);
        if (ret != LD2410D_OK) {
            LOG_E ("结束配置模式失败 (ret=%d)", ret);
        }
        set_state_machine_state (LD2410D_STATE_CONFIGURE);
        break;

    default:
        break;
    }
}

/**
 * @brief 阶段 2: 参数配置 (CONFIGURE)
 *
 * 步骤:
 *   0. 使能配置模式
 *   1. 设置最大探测距离
 *   2. 设置目标消失延迟
 *   3. 设置输出模式为工程模式
 *   4. 保存参数
 *   5. 结束配置模式
 */
static void process_state_configure (void)
{
    int8_t ret;

    ld2410d.sub_step++;

    switch (ld2410d.sub_step) {

    case 1:
        LOG_I ("使能配置模式...");
        ret = ld2410d_enable_config (&ld2410d.dev);
        if (ret != LD2410D_OK) {
            enter_error_state ("使能配置模式失败");
            return;
        }
        break;

    case 2:
        /* 设置最大探测距离: 8.5m → 85 (单位 0.1m) */
        ld2410d.dev.basic_cfg.max_distance = 85;
        LOG_I ("设置最大距离: 8.5m (0x%02X)",
               ld2410d.dev.basic_cfg.max_distance);
        ret = ld2410d_write_param (&ld2410d.dev,
                                    LD2410D_PARAM_MAX_DISTANCE,
                                    ld2410d.dev.basic_cfg.max_distance);
        if (ret != LD2410D_OK) {
            LOG_E ("设置最大距离失败 (ret=%d)", ret);
        }
        break;

    case 3:
        /* 设置目标消失延迟: 30 秒 */
        ld2410d.dev.basic_cfg.disappear_delay = 30;
        LOG_I ("设置消失延迟: %u s", ld2410d.dev.basic_cfg.disappear_delay);
        ret = ld2410d_write_param (&ld2410d.dev,
                                    LD2410D_PARAM_DISAPPEAR_DELAY,
                                    ld2410d.dev.basic_cfg.disappear_delay);
        if (ret != LD2410D_OK) {
            LOG_E ("设置消失延迟失败 (ret=%d)", ret);
        }
        break;

    case 4:
        /* 切换输出模式为工程模式 (获取完整能量值数据) */
        LOG_I ("设置输出模式: 工程模式");
        ret = ld2410d_set_output_mode (&ld2410d.dev,
                                        LD2410D_OUTPUT_MODE_ENGINEERING);
        if (ret != LD2410D_OK) {
            LOG_E ("设置输出模式失败 (ret=%d)", ret);
        } else {
            ld2410d.dev.output_mode = 1;
            ld2410d.result.output_ok = 1;
        }
        break;

    case 5:
        /* 保存参数到 Flash */
        LOG_I ("保存参数...");
        ret = ld2410d_save_params (&ld2410d.dev);
        if (ret != LD2410D_OK) {
            LOG_E ("保存参数失败 (ret=%d)", ret);
        }
        break;

    case 6:
        LOG_I ("结束配置模式...");
        ret = ld2410d_end_config (&ld2410d.dev);
        if (ret != LD2410D_OK) {
            LOG_E ("结束配置模式失败 (ret=%d)", ret);
        }
        ld2410d.result.config_ok = 1;
        set_state_machine_state (LD2410D_STATE_OPERATION);
        LOG_I ("配置完成, 进入运行模式");
        break;

    default:
        break;
    }
}

/**
 * @brief 阶段 3: 正常运行 (OPERATION)
 *
 * 周期性接收工程模式数据帧, 解析目标状态、距离和能量值。
 *
 * 运行逻辑:
 *   - 每轮 process() 尝试接收一帧工程数据
 *   - 数据帧更新周期约 165ms, 因此设置 200ms 超时
 *   - 检测到目标状态变化时触发回调
 *   - 每 10 秒打印一次能量值摘要
 */
static void process_state_operation (void)
{
    int8_t                        ret;
    ld2410d_engineering_data_t    data;
    uint64_t                      now_ms;

    now_ms = get_time_ms ();

    /* 尝试接收一帧工程模式数据 */
    ret = ld2410d_recv_engineering_frame (&ld2410d.dev, &data, 200);

    if (ret == LD2410D_OK) {
        /* 缓存最新数据 */
        ld2410d.dev.eng_data = data;

        /* 检测状态变化并上报 */
        check_and_report_target (data.status, data.distance);

        /* 周期性输出能量值摘要 (每 10 秒) */
        if (now_ms - ld2410d.last_report_ms > 10000U) {
            ld2410d.last_report_ms = now_ms;

            LOG_I ("--- 能量值摘要 ---");
            for (int i = 0; i < 16; i++) {
                uint16_t motion_db = ld2410d_energy_to_db (
                    data.gates[i].motion_energy);
                uint16_t static_db = ld2410d_energy_to_db (
                    data.gates[i].static_energy);

                if (data.gates[i].motion_energy > 0 ||
                    data.gates[i].static_energy > 0) {
                    LOG_I ("  门%2d  运动: %u.%02u dB  静止: %u.%02u dB",
                           i,
                           motion_db / 100, motion_db % 100,
                           static_db / 100, static_db % 100);
                }
            }
        }
    } else if (ret == LD2410D_ERR_TIMEOUT) {
        /* 超时正常 (数据帧间隔 165ms) */
    } else {
        LOG_E ("接收工程数据帧错误 (ret=%d)", ret);
    }
}

/* ==================== 公开接口 ==================== */

/**
 * @brief 初始化 LD2410D 应用
 *
 * 设置初始状态为 INIT, 状态机在 process() 中驱动。
 *
 * @note 调用前需先填充 ld2410d.dev.uart (send / recv / flush)
 */
void ld2410d_app_init (void)
{
    memset (&ld2410d, 0, sizeof (ld2410d));
    ld2410d.task_state     = LD2410D_STATE_INIT;
    ld2410d.last_state     = LD2410D_STATE_INIT;
    ld2410d.last_status    = LD2410D_DETECT_NONE;
    ld2410d.last_distance  = 0;
    ld2410d.last_report_ms = 0;
    LOG_I ("LD2410D 应用初始化完成, 状态机启动");
}

/**
 * @brief 处理 LD2410D 应用 (需在主循环中周期性调用)
 *
 * 状态机驱动函数, 按序完成各阶段:
 *   INIT → CHECK → CONFIGURE → OPERATION
 *
 * 典型调用方式:
 *   int main (void) {
 *       // 注册 UART 操作集
 *       ld2410d.dev.uart.send  = my_uart_send;
 *       ld2410d.dev.uart.recv  = my_uart_recv;
 *       ld2410d.dev.uart.flush = my_uart_flush;
 *
 *       // 注册目标回调
 *       ld2410d.target_callback = my_target_handler;
 *
 *       ld2410d_app_init ();
 *       while (1) {
 *           ld2410d_app_process ();
 *           // 其他任务...
 *       }
 *   }
 */
void ld2410d_app_process (void)
{
    enum ld2410d_app_states_e this_state;
    uint64_t                  now_ms;

    this_state = ld2410d.task_state;

    /* 状态切换同步 */
    ld2410d.last_state = this_state;

    now_ms = get_time_ms ();

    /* 阶段处理 */
    switch (this_state) {

    case LD2410D_STATE_INIT:
        process_state_init ();
        break;

    case LD2410D_STATE_CHECK:
        process_state_check ();
        break;

    case LD2410D_STATE_CONFIGURE:
        process_state_configure ();
        break;

    case LD2410D_STATE_OPERATION:
        process_state_operation ();
        break;

    case LD2410D_STATE_IDLE:
        if (ld2410d.last_state != LD2410D_STATE_IDLE) {
            LOG_I ("进入空闲状态");
        }
        break;

    case LD2410D_STATE_TIMEOUT:
        if (ld2410d.last_state != LD2410D_STATE_TIMEOUT) {
            LOG_E ("超时, 状态机停止");
        }
        break;

    case LD2410D_STATE_ERROR:
        if (ld2410d.last_state != LD2410D_STATE_ERROR) {
            LOG_E ("致命错误, 状态机停止");
        }
        break;

    default:
        break;
    }

    /* 超时监控 */
    if (ld2410d.timeout) {
        if (now_ms >= ld2410d.timeout) {
            set_state_machine_state (ld2410d.timeout_state);
        }
    }
}
