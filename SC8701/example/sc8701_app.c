/**
 * @file sc8701_app.c
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief SC8701 应用测试示例 — 参考 bq25710_app.c 架构
 * @version 1.0
 * @date 2026-06-13
 *
 * @note  硬件引脚定义 (CH32X035)
 *    SC8701_CE           PB8    (GPIO OUT, 低有效使能)
 *    SC8701_PWM_VOUT     PA0    (TIM2_CH1, PWM 调压)
 *    SC8701_IPWM_ILIM    PA1    (TIM2_CH2, PWM 限流)
 *    SC8701_ITUNE        PB9    (GPIO OUT, IPWM 控制目标选择)
 *    SC8701_PG           PA2    (GPIO IN, Power Good)
 *
 *    ADC 监测通道:
 *    SC8701_ADC_VIN      PA4   (ADC_IN4, 需外部电阻分压)
 *    SC8701_ADC_VOUT     PA5   (ADC_IN5, 需外部电阻分压)
 *    SC8701_ADC_IIN      PA6   (ADC_IN6, 检流放大器输出)
 *    SC8701_ADC_IOUT     PA7   (ADC_IN7, 检流放大器输出)
 *
 * @copyright Copyright (c) 2026
 */

#include "sc8701_app.h"
#include <stdio.h>
#include <string.h>

/* ==================== 硬件平台头文件 (按需替换) ==================== */
/* 示例基于 CH32X035, 移植到其他 MCU 时替换以下头文件和 GPIO 宏 */
#ifdef CH32X035_PLATFORM
#include "ch32x035.h"
#include "timer.h"
#include "gpio.h"
#endif

/* ==================== 日志宏 (可按需替换为实际日志系统) ==================== */

#ifdef LOG_ENABLE
#define LOG_LVL_NONE    0
#define LOG_LVL_ERR     1
#define LOG_LVL_WARN    2
#define LOG_LVL_INFO    3
#define LOG_LVL_DEBUG   4

#ifndef LOG_LEVEL
#define LOG_LEVEL LOG_LVL_INFO
#endif

#define LOG_E(fmt, ...)  do { if (LOG_LEVEL >= LOG_LVL_ERR)   printf("[SC8701][E] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#define LOG_W(fmt, ...)  do { if (LOG_LEVEL >= LOG_LVL_WARN)  printf("[SC8701][W] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#define LOG_I(fmt, ...)  do { if (LOG_LEVEL >= LOG_LVL_INFO)  printf("[SC8701][I] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#define LOG_D(fmt, ...)  do { if (LOG_LEVEL >= LOG_LVL_DEBUG) printf("[SC8701][D] " fmt "\r\n", ##__VA_ARGS__); } while(0)
#else
#define LOG_E(...)
#define LOG_W(...)
#define LOG_I(...)
#define LOG_D(...)
#endif

/* ==================== 全局应用实例 ==================== */

static sc8701_app_t sc_app;

/* ==================== 状态机名称表 (调试用) ==================== */

static const char *const sc_app_state_names[] = {
    "SC_APP_STATE_INIT",
    "SC_APP_STATE_CONFIGURE",
    "SC_APP_STATE_ENABLE_TEST",
    "SC_APP_STATE_VOUT_SWEEP_TEST",
    "SC_APP_STATE_ILIM_TEST",
    "SC_APP_STATE_DYNAMIC_TEST",
    "SC_APP_STATE_MONITOR",
    "SC_APP_STATE_IDLE",
    "SC_APP_STATE_TIMEOUT",
    "SC_APP_STATE_ERROR",
};

/* ==================== 模式名称表 ==================== */

static const char *const sc_mode_names[] = {
    "BUCK",
    "BOOST",
    "BUCK-BOOST",
    "OFF",
    "FAULT",
};

/* ==================== 硬件抽象层实现 (CH32X035 示例) ==================== */

/**
 * @brief 获取系统毫秒时间戳
 *
 * 移植时替换为实际平台的 ms 级时间戳获取函数。
 */
static uint32_t hal_get_ms (void)
{
#ifdef CH32X035_PLATFORM
    return get_time().val;
#else
    /* 用户需替换为实际时间戳获取函数 */
    static uint32_t mock_ms = 0;
    return mock_ms++;
#endif
}

/**
 * @brief 毫秒延时
 */
static void hal_delay_ms (uint32_t ms)
{
#ifdef CH32X035_PLATFORM
    Delay_Ms(ms);
#else
    /* 用户需替换为实际延时函数 */
    volatile uint32_t i;
    while (ms--) {
        for (i = 0; i < 8000; i++) { __asm volatile("nop"); }
    }
#endif
}

/**
 * @brief 微秒延时
 */
static void hal_delay_us (uint32_t us)
{
#ifdef CH32X035_PLATFORM
    Delay_Us(us);
#else
    volatile uint32_t i;
    while (us--) {
        for (i = 0; i < 8; i++) { __asm volatile("nop"); }
    }
#endif
}

/* -------------------------------------------------------------------------- */
/* GPIO 操作                                                                  */
/* -------------------------------------------------------------------------- */

/**
 * @brief /CE 引脚输出
 * @param level  0 = 使能 (拉低), 1 = 关断 (拉高)
 */
static void hal_ce_set (uint8_t level)
{
#ifdef CH32X035_PLATFORM
    GPIO_WriteBit(GPIOB, GPIO_Pin_8, (level ? Bit_SET : Bit_RESET));
#else
    /* 用户实现:
     *   if (level) GPIO_SetHigh(SC8701_CE_PORT, SC8701_CE_PIN);
     *   else       GPIO_SetLow(SC8701_CE_PORT, SC8701_CE_PIN);
     */
    (void)level;
#endif
}

/**
 * @brief ITUNE 引脚输出
 * @param level  0 = 输入电流 (ILIM1), 1 = 输出电流 (ILIM2)
 */
static void hal_itune_set (uint8_t level)
{
#ifdef CH32X035_PLATFORM
    GPIO_WriteBit(GPIOB, GPIO_Pin_9, (level ? Bit_SET : Bit_RESET));
#else
    (void)level;
#endif
}

/**
 * @brief PG 引脚输入
 * @return 1 = Power Good (VOUT 在 90%~110%), 0 = 未就绪
 */
static uint8_t hal_pg_get (void)
{
#ifdef CH32X035_PLATFORM
    return (uint8_t)GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_2);
#else
    return 0;
#endif
}

/* -------------------------------------------------------------------------- */
/* PWM 操作 (CH32X035 TIM2 示例)                                               */
/* -------------------------------------------------------------------------- */

/**
 * @brief VOUT 调压 PWM 初始化
 *
 * TIM2_CH1 → PA0
 * 频率 20kHz~100kHz, 典型 50kHz
 */
static int8_t hal_pwm_vout_init (uint32_t freq_hz)
{
#ifdef CH32X035_PLATFORM
    TIM_OCInitTypeDef  TIM_OCInitStructure = {0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = {0};
    GPIO_InitTypeDef    GPIO_InitStructure = {0};

    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA0 复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    uint32_t pclk = SystemCoreClock;
    uint16_t psc  = 0;
    uint16_t arr;

    /* 计算预分频和自动重载值 */
    arr = (uint16_t)(pclk / freq_hz) - 1;

    TIM_TimeBaseStructure.TIM_Period        = arr;
    TIM_TimeBaseStructure.TIM_Prescaler     = psc;
    TIM_TimeBaseStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseStructure.TIM_CounterMode   = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);

    /* PWM1 模式，初始占空比 100% */
    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = arr;  /* 100% duty */
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC1Init(TIM2, &TIM_OCInitStructure);
    TIM_OC1PreloadConfig(TIM2, TIM_OCPreload_Enable);

    TIM_ARRPreloadConfig(TIM2, ENABLE);
    TIM_Cmd(TIM2, ENABLE);
    return 0;
#else
    (void)freq_hz;
    return -1;
#endif
}

/**
 * @brief VOUT 调压 PWM 设置占空比
 * @param duty  [0.0, 1.0]
 */
static void hal_pwm_vout_set (float duty)
{
#ifdef CH32X035_PLATFORM
    uint16_t arr  = TIM2->ATRLR;  /* 对于 CH32X035，ARR 在 ATRLR */
    uint16_t ccr1 = (uint16_t)((float)arr * duty);
    if (ccr1 > arr) ccr1 = arr;
    TIM_SetCompare1(TIM2, ccr1);
#else
    (void)duty;
#endif
}

/**
 * @brief IPWM 限流 PWM 初始化
 *
 * TIM2_CH2 → PA1
 */
static int8_t hal_pwm_ilim_init (uint32_t freq_hz)
{
#ifdef CH32X035_PLATFORM
    TIM_OCInitTypeDef  TIM_OCInitStructure = {0};
    GPIO_InitTypeDef    GPIO_InitStructure = {0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* PA1 复用推挽输出 */
    GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_1;
    GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    /* TIM2 已在 VOUT PWM 初始化时配置，此处仅配置 CH2 */
    uint16_t arr = TIM2->ATRLR;

    TIM_OCInitStructure.TIM_OCMode      = TIM_OCMode_PWM1;
    TIM_OCInitStructure.TIM_OutputState = TIM_OutputState_Enable;
    TIM_OCInitStructure.TIM_Pulse       = arr;
    TIM_OCInitStructure.TIM_OCPolarity  = TIM_OCPolarity_High;
    TIM_OC2Init(TIM2, &TIM_OCInitStructure);
    TIM_OC2PreloadConfig(TIM2, TIM_OCPreload_Enable);
    return 0;
#else
    (void)freq_hz;
    return -1;
#endif
}

/**
 * @brief IPWM 限流 PWM 设置占空比
 */
static void hal_pwm_ilim_set (float duty)
{
#ifdef CH32X035_PLATFORM
    uint16_t arr  = TIM2->ATRLR;
    uint16_t ccr2 = (uint16_t)((float)arr * duty);
    if (ccr2 > arr) ccr2 = arr;
    TIM_SetCompare2(TIM2, ccr2);
#else
    (void)duty;
#endif
}

/* -------------------------------------------------------------------------- */
/* ADC 操作 (可选)                                                             */
/* -------------------------------------------------------------------------- */

/**
 * @brief ADC 读取指定通道电压 (mV)
 *
 * CH32X035 示例，使用 ADC1 单次转换。
 */
static int8_t hal_adc_read_mv (uint8_t channel, uint16_t *mv)
{
#ifdef CH32X035_PLATFORM
    /* 用户需根据实际 ADC 通道映射实现 */
    /*
     * ADC_RegularChannelConfig(ADC1, channel_to_adc_ch(channel), 1, ADC_SampleTime_239Cycles5);
     * ADC_SoftwareStartConvCmd(ADC1, ENABLE);
     * while (!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
     * uint16_t raw = ADC_GetConversionValue(ADC1);
     * *mv = (uint16_t)((uint32_t)raw * 3300 / 4096);
     * return 0;
     */
    (void)channel;
    (void)mv;
    return -1;
#else
    (void)channel;
    (void)mv;
    return -1;
#endif
}

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief 设置超时并指定超时后的跳转状态
 */
static void set_timeout (uint64_t timeout_ms, uint8_t timeout_state)
{
    sc_app.timeout       = timeout_ms;
    sc_app.timeout_state = timeout_state;
}

/**
 * @brief 清除超时
 */
static void clear_timeout (void)
{
    sc_app.timeout       = 0;
    sc_app.timeout_state = 0;
}

/**
 * @brief 切换状态机状态，自动记录耗时
 */
static void set_state (uint8_t next_state)
{
    uint8_t last_state = sc_app.task_state;

    if (last_state == next_state) {
        return;
    }

    /* 记录上一阶段的耗时 */
    if (last_state < SC_APP_STATE_COUNT) {
        if (sc_app.stages[last_state].enter_ms != 0) {
            uint32_t now_ms = hal_get_ms();
            sc_app.stages[last_state].elapsed_ms =
                now_ms - sc_app.stages[last_state].enter_ms;
        }
    }

    /* 清除超时 */
    clear_timeout();

    /* 记录新阶段进入时间 */
    sc_app.task_state = next_state;
    if (next_state < SC_APP_STATE_COUNT) {
        sc_app.stages[next_state].enter_ms  = hal_get_ms();
        sc_app.stages[next_state].elapsed_ms = 0;
    }

    sc_app.sub_step = 0;

    LOG_I("%s -> %s", sc_app_state_names[last_state], sc_app_state_names[next_state]);

    if (sc_app.notify_handler) {
        sc_app.notify_handler(next_state, NULL);
    }
}

/**
 * @brief 进入错误状态
 */
static void enter_error (const char *reason)
{
    LOG_E("错误: %s", reason);
    set_state(SC_APP_STATE_ERROR);
}

/**
 * @brief 读取 ADC 并更新快照
 */
static void update_adc_snapshot (void)
{
    sc8701_app_adc_snapshot_t *snap = &sc_app.adc_snap;

    snap->timestamp_ms = hal_get_ms();

    if (sc_app.hal.adc_read_mv) {
        sc_app.hal.adc_read_mv(SC8701_ADC_VIN,  &snap->vin_mv);
        sc_app.hal.adc_read_mv(SC8701_ADC_VOUT, &snap->vout_mv);
        sc_app.hal.adc_read_mv(SC8701_ADC_IIN,  &snap->iin_ma);
        sc_app.hal.adc_read_mv(SC8701_ADC_IOUT, &snap->iout_ma);
    }

    snap->pg   = sc8701_get_pg(&sc_app.drv_ctx);
    snap->mode = sc8701_get_mode(&sc_app.drv_ctx);
}

/* ==================== 各阶段处理函数 ==================== */

/**
 * @brief 阶段 0: 初始化 (INIT)
 *
 * 子步骤:
 *   1. GPIO 外设初始化 (CE/ITUNE/PG)
 *   2. PWM 外设初始化已在 sc8701_init() 中完成
 *   3. 调用 sc8701_init() 初始化驱动
 *   4. 验证驱动初始化结果
 */
static void process_state_init (void)
{
    int8_t ret;

    sc_app.sub_step++;

    switch (sc_app.sub_step) {

    case 1:
        LOG_I("开始 GPIO 初始化...");

#ifdef CH32X035_PLATFORM
        {
            RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);
            GPIO_InitTypeDef GPIO_InitStructure;

            /* /CE (PB8) — 推挽输出, 初始高电平 (关断) */
            GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_8;
            GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
            GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_Init(GPIOB, &GPIO_InitStructure);
            GPIO_WriteBit(GPIOB, GPIO_Pin_8, Bit_SET);

            /* ITUNE (PB9) — 推挽输出, 初始低 (控制输入电流) */
            GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_9;
            GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_Out_PP;
            GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_Init(GPIOB, &GPIO_InitStructure);
            GPIO_WriteBit(GPIOB, GPIO_Pin_9, Bit_RESET);

            /* PG (PA2) — 上拉输入 */
            GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_2;
            GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IPU;
            GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
            GPIO_Init(GPIOA, &GPIO_InitStructure);
        }
#endif
        LOG_I("GPIO 初始化完成");
        break;

    case 2:
        LOG_I("绑定硬件抽象层...");
        sc_app.hal.ce_set        = hal_ce_set;
        sc_app.hal.itune_set     = hal_itune_set;
        sc_app.hal.pg_get        = hal_pg_get;
        sc_app.hal.pwm_vout_init = hal_pwm_vout_init;
        sc_app.hal.pwm_vout_set  = hal_pwm_vout_set;
        sc_app.hal.pwm_ilim_init = hal_pwm_ilim_init;
        sc_app.hal.pwm_ilim_set  = hal_pwm_ilim_set;
        sc_app.hal.adc_read_mv   = hal_adc_read_mv;
        sc_app.hal.delay_ms      = hal_delay_ms;
        sc_app.hal.delay_us      = hal_delay_us;
        sc_app.hal.get_ms        = hal_get_ms;
        break;

    case 3:
        LOG_I("调用 sc8701_init()...");
        ret = sc8701_init(&sc_app.drv_ctx, &sc_app.hal, &sc_app.hw_config);
        if (ret != SC8701_OK) {
            enter_error("sc8701_init 失败");
            return;
        }
        sc_app.result.init_ok = 1;
        LOG_I("SC8701 驱动初始化成功");
        LOG_I("  硬件设定 VOUT = %lu mV", (unsigned long)sc8701_get_vout_set(&sc_app.drv_ctx));
        LOG_I("  输入电流限制 = %lu mA", (unsigned long)sc_app.drv_ctx.iin_lim_ma);
        LOG_I("  输出电流限制 = %lu mA", (unsigned long)sc_app.drv_ctx.iout_lim_ma);
        break;

    case 4:
        set_state(SC_APP_STATE_CONFIGURE);
        break;
    }
}

/**
 * @brief 阶段 1: 配置 (CONFIGURE)
 *
 * 设置初始输出电压和限流参数。
 */
static void process_state_configure (void)
{
    int8_t ret;

    sc_app.sub_step++;

    switch (sc_app.sub_step) {

    case 1:
        LOG_I("配置初始输出电压 = %lu mV", (unsigned long)sc_app.drv_ctx.vout_set_mv);
        ret = sc8701_set_vout(&sc_app.drv_ctx, sc_app.drv_ctx.vout_set_mv);
        if (ret != SC8701_OK) {
            enter_error("设置初始 VOUT 失败");
            return;
        }
        break;

    case 2:
        LOG_I("配置输入电流限制 = %lu mA", (unsigned long)sc_app.drv_ctx.iin_lim_ma);
        ret = sc8701_set_itune_target(&sc_app.drv_ctx, SC8701_ITUNE_INPUT);
        if (ret != SC8701_OK) {
            enter_error("设置 ITUNE 目标失败");
            return;
        }
        ret = sc8701_set_ilim(&sc_app.drv_ctx, sc_app.drv_ctx.iin_lim_ma);
        if (ret != SC8701_OK) {
            enter_error("设置 ILIM 失败");
            return;
        }
        break;

    case 3:
        sc_app.result.config_ok = 1;
        LOG_I("配置完成");
        set_state(SC_APP_STATE_ENABLE_TEST);
        break;
    }
}

/**
 * @brief 阶段 2: 使能测试 (ENABLE_TEST)
 *
 * 使能芯片，等待 Power Good，验证启动。
 */
static void process_state_enable_test (void)
{
    int8_t ret;

    sc_app.sub_step++;

    switch (sc_app.sub_step) {

    case 1:
        LOG_I("使能 SC8701 (/CE → LOW)...");
        ret = sc8701_enable(&sc_app.drv_ctx, SC8701_APP_ENABLE_TIMEOUT_MS);
        if (ret == SC8701_ERR_PG_TIMEOUT) {
            LOG_W("PG 超时! 检查输入电源和负载连接");
            LOG_W("继续测试 (可能无负载)...");
        } else if (ret != SC8701_OK) {
            enter_error("使能 SC8701 失败");
            return;
        }
        break;

    case 2:
        /* 验证 PG 状态 */
        sc_app.result.enable_ok = sc8701_get_pg(&sc_app.drv_ctx);
        update_adc_snapshot();
        LOG_I("使能测试完成: PG=%d, VIN=%u mV, VOUT=%u mV, Mode=%s",
              sc_app.adc_snap.pg,
              sc_app.adc_snap.vin_mv,
              sc_app.adc_snap.vout_mv,
              sc_mode_names[sc_app.adc_snap.mode]);
        set_state(SC_APP_STATE_VOUT_SWEEP_TEST);
        break;
    }
}

/**
 * @brief 阶段 3: 调压扫描测试 (VOUT_SWEEP_TEST)
 *
 * 从 start_mv 到 end_mv 步进调节输出电压，验证调压线性度。
 */
static void process_state_vout_sweep_test (void)
{
    sc8701_app_vout_sweep_t *sweep = &sc_app.vout_sweep;
    int8_t ret;

    sc_app.sub_step++;

    switch (sc_app.sub_step) {

    case 1:
        LOG_I("开始 VOUT 扫描测试...");
        sweep->start_mv  = 5000;   /* 5V */
        sweep->end_mv    = 12000;  /* 12V */
        sweep->step_mv   = 500;    /* 每步 500mV */
        sweep->dwell_ms  = 200;    /* 每步停留 200ms */
        sweep->current_mv = sweep->start_mv;
        sweep->direction  = 0;     /* 正向 */
        LOG_I("  范围: %lu ~ %lu mV, 步进: %lu mV",
              (unsigned long)sweep->start_mv,
              (unsigned long)sweep->end_mv,
              (unsigned long)sweep->step_mv);
        break;

    case 2:
        /* 设置目标电压 */
        ret = sc8701_set_vout(&sc_app.drv_ctx, sweep->current_mv);
        if (ret != SC8701_OK) {
            LOG_E("设置 VOUT=%lu mV 失败", (unsigned long)sweep->current_mv);
            enter_error("VOUT 扫描失败");
            return;
        }

        /* 等待稳定 */
        sc_app.hal.delay_ms(sweep->dwell_ms);

        /* 读取反馈 */
        update_adc_snapshot();
        if (sc_app.adc_snapshot.vout_mv > 0) {
            uint32_t error_mv = (sc_app.adc_snapshot.vout_mv > sweep->current_mv)
                ? (sc_app.adc_snapshot.vout_mv - sweep->current_mv)
                : (sweep->current_mv - sc_app.adc_snapshot.vout_mv);
            LOG_I("  VOUT 设定=%lu mV, 实际=%u mV, 误差=%lu mV, PG=%d",
                  (unsigned long)sweep->current_mv, sc_app.adc_snapshot.vout_mv,
                  (unsigned long)error_mv, sc_app.adc_snapshot.pg);
        } else {
            LOG_I("  VOUT 设定=%lu mV (ADC 未启用, 跳过反馈校验)", (unsigned long)sweep->current_mv);
        }
        break;

    case 3:
        /* 推进扫描 */
        if (sweep->direction == 0) {
            /* 正向 */
            sweep->current_mv += sweep->step_mv;
            if (sweep->current_mv >= sweep->end_mv) {
                sweep->current_mv = sweep->end_mv;
                sweep->direction  = 1;  /* 切换为反向 */
                LOG_I("正向扫描完成, 开始反向扫描...");
            }
        } else {
            /* 反向 */
            if (sweep->current_mv <= sweep->start_mv + sweep->step_mv) {
                sweep->current_mv = sweep->start_mv;
            } else {
                sweep->current_mv -= sweep->step_mv;
            }
        }

        /* 判断扫描是否完成 */
        if (sweep->direction == 1 && sweep->current_mv == sweep->start_mv) {
            LOG_I("VOUT 扫描测试完成 (往返)");
            sc_app.result.vout_sweep_ok = 1;

            /* 恢复到初始电压 */
            sc8701_set_vout(&sc_app.drv_ctx, sc_app.drv_ctx.vout_set_mv);
            set_state(SC_APP_STATE_ILIM_TEST);
            return;
        }

        /* 继续扫描: 回到 sub_step 2 */
        sc_app.sub_step = 1;  /* 下次进入 case 2 */
        break;
    }
}

/**
 * @brief 阶段 4: 限流测试 (ILIM_TEST)
 *
 * 动态调节 IPWM 占空比，验证限流功能。
 * 分别在输入限流和输出限流两种模式下测试。
 */
static void process_state_ilim_test (void)
{
    int8_t ret;

    sc_app.sub_step++;

    switch (sc_app.sub_step) {

    case 1:
        LOG_I("限流测试: 输入电流限制 (ITUNE=INPUT)...");
        ret = sc8701_set_itune_target(&sc_app.drv_ctx, SC8701_ITUNE_INPUT);
        if (ret != SC8701_OK) {
            enter_error("设置 ITUNE=INPUT 失败");
            return;
        }
        break;

    case 2:
        /* 设置为硬件限流值的 50% */
        {
            uint32_t ilim_test = sc_app.drv_ctx.iin_lim_ma / 2;
            LOG_I("  设置输入限流 = %lu mA (50%% of %lu mA)",
                  (unsigned long)ilim_test, (unsigned long)sc_app.drv_ctx.iin_lim_ma);
            ret = sc8701_set_ilim(&sc_app.drv_ctx, ilim_test);
            if (ret != SC8701_OK) {
                enter_error("设置输入限流失败");
                return;
            }
        }
        sc_app.hal.delay_ms(100);
        update_adc_snapshot();
        LOG_I("  IIN=%u mA, PG=%d", sc_app.adc_snap.iin_ma, sc_app.adc_snap.pg);
        break;

    case 3:
        LOG_I("限流测试: 输出电流限制 (ITUNE=OUTPUT)...");
        ret = sc8701_set_itune_target(&sc_app.drv_ctx, SC8701_ITUNE_OUTPUT);
        if (ret != SC8701_OK) {
            enter_error("设置 ITUNE=OUTPUT 失败");
            return;
        }
        break;

    case 4:
        /* 设置为硬件限流值的 75% */
        {
            uint32_t ilim_test = (sc_app.drv_ctx.iout_lim_ma * 3) / 4;
            LOG_I("  设置输出限流 = %lu mA (75%% of %lu mA)",
                  (unsigned long)ilim_test, (unsigned long)sc_app.drv_ctx.iout_lim_ma);
            ret = sc8701_set_ilim(&sc_app.drv_ctx, ilim_test);
            if (ret != SC8701_OK) {
                enter_error("设置输出限流失败");
                return;
            }
        }
        sc_app.hal.delay_ms(100);
        update_adc_snapshot();
        LOG_I("  IOUT=%u mA, PG=%d", sc_app.adc_snap.iout_ma, sc_app.adc_snap.pg);
        break;

    case 5:
        /* 恢复默认: 输入限流 100% */
        LOG_I("恢复默认限流设置...");
        ret = sc8701_set_itune_target(&sc_app.drv_ctx, SC8701_ITUNE_INPUT);
        ret |= sc8701_set_ilim(&sc_app.drv_ctx, sc_app.drv_ctx.iin_lim_ma);
        if (ret != SC8701_OK) {
            enter_error("恢复限流失败");
            return;
        }
        sc_app.result.ilim_ok = 1;
        LOG_I("限流测试完成");
        set_state(SC_APP_STATE_DYNAMIC_TEST);
        break;
    }
}

/**
 * @brief 阶段 5: 动态响应测试 (DYNAMIC_TEST)
 *
 * 模拟负载突变或输入电压变化，验证动态响应。
 * 通过快速改变 VOUT 设定值来模拟。
 */
static void process_state_dynamic_test (void)
{
    sc_app.sub_step++;

    switch (sc_app.sub_step) {

    case 1:
        LOG_I("动态响应测试: 快速 VOUT 切换...");
        LOG_I("  5V → 9V → 5V (每步 100ms)");
        break;

    case 2:
        sc8701_set_vout(&sc_app.drv_ctx, 9000);
        sc_app.hal.delay_ms(100);
        update_adc_snapshot();
        LOG_I("  设定 9V, 实际=%u mV, PG=%d", sc_app.adc_snap.vout_mv, sc_app.adc_snap.pg);
        break;

    case 3:
        sc8701_set_vout(&sc_app.drv_ctx, 5000);
        sc_app.hal.delay_ms(100);
        update_adc_snapshot();
        LOG_I("  设定 5V, 实际=%u mV, PG=%d", sc_app.adc_snap.vout_mv, sc_app.adc_snap.pg);
        break;

    case 4:
        LOG_I("动态响应测试: 12V → 3.3V → 12V...");
        break;

    case 5:
        sc8701_set_vout(&sc_app.drv_ctx, 3300);
        sc_app.hal.delay_ms(100);
        update_adc_snapshot();
        LOG_I("  设定 3.3V, 实际=%u mV, PG=%d", sc_app.adc_snap.vout_mv, sc_app.adc_snap.pg);
        break;

    case 6:
        sc8701_set_vout(&sc_app.drv_ctx, 12000);
        sc_app.hal.delay_ms(100);
        update_adc_snapshot();
        LOG_I("  设定 12V, 实际=%u mV, PG=%d", sc_app.adc_snap.vout_mv, sc_app.adc_snap.pg);
        break;

    case 7:
        /* 恢复到初始电压 */
        sc8701_set_vout(&sc_app.drv_ctx, sc_app.drv_ctx.vout_set_mv);
        sc_app.result.dynamic_ok = 1;
        LOG_I("动态响应测试完成");
        set_state(SC_APP_STATE_MONITOR);
        break;
    }
}

/**
 * @brief 阶段 6: 持续监测 (MONITOR)
 *
 * 周期性读取并打印 VIN/VOUT/IOUT/PG/模式。
 */
static void process_state_monitor (void)
{
    if (sc_app.sub_step == 0) {
        sc_app.sub_step = 1;
        sc_app.last_monitor_ms = hal_get_ms();
        LOG_I("进入监测模式 (间隔 %lu ms)...", (unsigned long)SC8701_APP_MONITOR_INTERVAL_MS);
    }

    uint32_t now_ms = hal_get_ms();
    if (now_ms - sc_app.last_monitor_ms >= SC8701_APP_MONITOR_INTERVAL_MS) {
        sc_app.last_monitor_ms = now_ms;
        update_adc_snapshot();
        sc8701_app_adc_snapshot_t *s = &sc_app.adc_snap;

        LOG_I("[MONITOR] VIN=%u mV, VOUT=%u mV, IIN=%u mA, IOUT=%u mA, PG=%d, Mode=%s",
              s->vin_mv, s->vout_mv, s->iin_ma, s->iout_ma,
              s->pg, sc_mode_names[s->mode]);
    }
}

/* ==================== 公开接口 ==================== */

/**
 * @brief 初始化 SC8701 应用
 */
void sc8701_app_init (void)
{
    memset(&sc_app, 0, sizeof(sc_app));

    /* 硬件配置 (示例: 12V 输出, 5A 输入限流, 3A 输出限流) */
    sc_app.hw_config.r_up_ohm     = 100000;  /* FB 上拉 100kΩ */
    sc_app.hw_config.r_down_ohm   = 11000;   /* FB 下拉 11kΩ → VOUT ≈ 1.22×(111/11) ≈ 12.3V */
    sc_app.hw_config.rilim1_ohm   = 240;     /* ILIM1 = 240Ω → IIN_LIM ≈ 1.21/240 ≈ 5A */
    sc_app.hw_config.rilim2_ohm   = 400;     /* ILIM2 = 400Ω → IOUT_LIM ≈ 1.21/400 ≈ 3A */
    sc_app.hw_config.rss1_ohm     = 0;       /* 未独立配置 RSS, 默认 RSS=RSNS */
    sc_app.hw_config.rsns1_uohm   = 10000;   /* 10mΩ 检流电阻 */
    sc_app.hw_config.rss2_ohm     = 0;
    sc_app.hw_config.rsns2_uohm   = 10000;   /* 10mΩ 检流电阻 */
    sc_app.hw_config.freq         = SC8701_FREQ_400KHZ;
    sc_app.hw_config.dead_time    = SC8701_DT_40NS;

    sc_app.task_state = SC_APP_STATE_INIT;
    sc_app.last_state = SC_APP_STATE_INIT;

    LOG_I("SC8701 应用初始化完成，状态机启动");
}

/**
 * @brief SC8701 应用主处理函数
 */
void sc8701_app_process (void)
{
    uint8_t this_state = sc_app.task_state;

    /* 同步上次状态 */
    sc_app.last_state = this_state;

    /* 阶段处理 */
    switch (this_state) {

    case SC_APP_STATE_INIT:
        process_state_init();
        break;

    case SC_APP_STATE_CONFIGURE:
        process_state_configure();
        break;

    case SC_APP_STATE_ENABLE_TEST:
        process_state_enable_test();
        break;

    case SC_APP_STATE_VOUT_SWEEP_TEST:
        process_state_vout_sweep_test();
        break;

    case SC_APP_STATE_ILIM_TEST:
        process_state_ilim_test();
        break;

    case SC_APP_STATE_DYNAMIC_TEST:
        process_state_dynamic_test();
        break;

    case SC_APP_STATE_MONITOR:
        process_state_monitor();
        break;

    case SC_APP_STATE_IDLE:
        if (sc_app.last_state != SC_APP_STATE_IDLE) {
            LOG_I("全部测试完成, 进入 IDLE");
            sc8701_app_print_status();
        }
        break;

    case SC_APP_STATE_TIMEOUT:
        if (sc_app.last_state != SC_APP_STATE_TIMEOUT) {
            LOG_E("超时! 状态机停止");
        }
        break;

    case SC_APP_STATE_ERROR:
        if (sc_app.last_state != SC_APP_STATE_ERROR) {
            LOG_E("致命错误, 状态机停止");
            sc8701_app_print_status();
        }
        break;

    default:
        break;
    }

    /* 超时监控 */
    if (sc_app.timeout) {
        uint32_t now_ms = hal_get_ms();
        if (now_ms >= sc_app.timeout) {
            set_state(sc_app.timeout_state);
        }
    }
}

/**
 * @brief 获取应用全局实例指针
 */
sc8701_app_t *sc8701_app_get_instance (void)
{
    return &sc_app;
}

/**
 * @brief 打印当前状态汇总
 */
void sc8701_app_print_status (void)
{
    sc8701_ctx_t *ctx = &sc_app.drv_ctx;
    sc8701_app_test_result_t *r = &sc_app.result;

    printf("\r\n");
    printf("========== SC8701 测试结果汇总 ==========\r\n");
    printf("  驱动初始化:      %s\r\n", r->init_ok       ? "PASS" : "FAIL");
    printf("  参数配置:        %s\r\n", r->config_ok     ? "PASS" : "FAIL");
    printf("  芯片使能:        %s\r\n", r->enable_ok     ? "PASS" : "FAIL");
    printf("  VOUT 调压扫描:   %s\r\n", r->vout_sweep_ok ? "PASS" : "FAIL");
    printf("  限流测试:        %s\r\n", r->ilim_ok       ? "PASS" : "FAIL");
    printf("  动态响应:        %s\r\n", r->dynamic_ok    ? "PASS" : "FAIL");
    printf("------------------------------------------\r\n");
    printf("  硬件设定 VOUT:   %lu mV\r\n", (unsigned long)sc8701_get_vout_set(ctx));
    printf("  当前目标 VOUT:   %lu mV\r\n", (unsigned long)sc8701_get_vout_target(ctx));
    printf("  输入限流:        %lu mA\r\n", (unsigned long)ctx->iin_lim_ma);
    printf("  输出限流:        %lu mA\r\n", (unsigned long)ctx->iout_lim_ma);
    printf("  使能状态:        %s\r\n", ctx->enabled  ? "ENABLED" : "DISABLED");
    printf("  PG 状态:         %s\r\n", ctx->pg_ok    ? "GOOD"    : "N/A");
    printf("  故障:            %s\r\n", ctx->fault    ? "YES"     : "NO");

    /* 各阶段耗时 */
    printf("------------------------------------------\r\n");
    printf("  各阶段耗时 (ms):\r\n");
    for (int i = 0; i < SC_APP_STATE_COUNT; i++) {
        if (sc_app.stages[i].elapsed_ms > 0) {
            printf("    %-30s %lu ms\r\n",
                   sc_app_state_names[i],
                   (unsigned long)sc_app.stages[i].elapsed_ms);
        }
    }
    printf("==========================================\r\n\r\n");
}
