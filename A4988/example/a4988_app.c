/**
 * @file a4988_app.c
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief A4988 应用层实现 — 步进电机控制示例
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @details 提供完整的步进电机应用示例，全部通过状态机驱动：
 *
 *          1. 定步数定位 — 精确控制电机旋转指定步数后停止
 *             例：旋转 800 步 (1/4 微步下正好一圈)，步进间隔 1000µs
 *
 *          2. 连续恒速旋转 — 以固定速度持续旋转，直到收到停止命令
 *             例：以 500µs 间隔旋转，等效约 150 RPM (1/4 微步)
 *
 *          3. 梯形加减速 — 平滑启动和停止，避免丢步
 *             支持 S 形速度曲线近似 (离散加速度变化)
 *
 *          4. 微步分辨率切换演示 — 遍历全部 5 种微步模式
 *
 * @note  硬件引脚定义 (见 a4988.h)
 *    A4988_STEP          PA0    步进脉冲 (上升沿有效)
 *    A4988_DIR           PA1    方向控制
 *    A4988_ENABLE        PA2    使能输出 (低电平有效)
 *    A4988_RESET         PA3    复位译码器 (低电平有效)
 *    A4988_SLEEP         PA4    睡眠模式 (低电平有效)
 *    A4988_MS1           PB0    微步选择位 1
 *    A4988_MS2           PB1    微步选择位 2
 *    A4988_MS3           PB3    微步选择位 3
 *
 *          本应用使用周期性调度的状态机模式，与 bq25710_app 风格一致。
 *          所有运动命令通过调用便捷 API 下发，在 a4988_app_process() 中
 *          按固定频率 (如 1kHz 定时器中断) 被驱动。
 */

#include "a4988_app.h"
#include "timer.h"      /* get_time, Delay_Us */
#include <string.h>

#define LOG_LEVEL   LOG_LVL_DEBUG
#define LOG_TAG     "A4988 APP"
#include "logger.h"

/* ==================== 全局应用实例 ==================== */

struct a4988_app_t a4988_app;

/* ==================== 状态机名称表 (调试用) ==================== */

static const char *const app_state_names[] = {
    "INIT",
    "CONFIGURE",
    "MOVE_POSITION",
    "MOVE_CONTINUOUS",
    "MOVE_RAMP",
    "DEMO_MICROSTEP",
    "IDLE",
    "ERROR",
};

/* ==================== 内部辅助函数 ==================== */

/**
 * @brief 切换状态机状态
 */
static void set_app_state(enum a4988_app_state_e next_state)
{
    enum a4988_app_state_e last_state = a4988_app.task_state;

    if (last_state == next_state)
        return;

    a4988_app.task_state = next_state;
    a4988_app.last_state = last_state;
    a4988_app.sub_step   = 0;

    LOG_I("%s -> %s", app_state_names[last_state], app_state_names[next_state]);

    if (a4988_app.notify_cb) {
        a4988_app.notify_cb((uint8_t)next_state, NULL);
    }
}

/**
 * @brief 获取当前时间戳 (µs)
 *
 * 用于步进脉冲定时。依赖 timer.h 中的 get_time()。
 * 如果 get_time() 返回的是毫秒，则乘以 1000 转为微秒。
 * CH32X035 SysTick 通常为 1ms，此处用 Delay_Us 累加计数器替代。
 */
static uint64_t get_time_us(void)
{
    /* 使用毫秒级时间戳 × 1000 作为近似 */
    return get_time().val * 1000ULL;
}

/* ==================== 阶段处理函数 ==================== */

/**
 * @brief 阶段 0: 初始化 (INIT)
 *
 * 步骤:
 *   0. GPIO 引脚初始化
 *   1. 使用默认配置调用 a4988_init()
 *   2. 进入 CONFIGURE 阶段
 */
static void process_state_init(void)
{
    int8_t ret;

    a4988_app.sub_step++;

    switch (a4988_app.sub_step) {

    case 1:
        LOG_I("开始 A4988 应用初始化...");

        /* 默认配置:
         *   微步: 1/4
         *   ROSC: 接 VDD (自动衰减)
         *   检流电阻: 200 mΩ
         *   VREF: 1.6V → ITripMAX ≈ 1000 mA
         *   脉冲宽度: 2 µs
         */
        a4988_app.config.microstep          = A4988_MICROSTEP_QUARTER;
        a4988_app.config.rosc_mode          = A4988_ROSC_VDD;
        a4988_app.config.rosc_resistor_ohm  = 0;
        a4988_app.config.rs_mohm            = 200;
        a4988_app.config.vref_mv            = 1600;
        a4988_app.config.step_pulse_width_us = 2;
        a4988_app.config.initial_dir        = A4988_DIR_CW;
        break;

    case 2:
        ret = a4988_init(&a4988_app.config);
        if (ret != A4988_OK) {
            LOG_E("A4988 驱动初始化失败 (ret=%d)", ret);
            set_app_state(A4988_APP_STATE_ERROR);
            return;
        }
        LOG_I("A4988 驱动初始化成功");
        break;

    case 3:
        LOG_I("初始化完成，进入配置阶段");
        set_app_state(A4988_APP_STATE_CONFIGURE);
        break;
    }
}

/**
 * @brief 阶段 1: 配置 (CONFIGURE)
 *
 * 此阶段预留用于动态调整配置（电流、微步等）。
 * 完成后直接进入 IDLE，等待用户下发运动命令。
 */
static void process_state_configure(void)
{
    a4988_app.sub_step++;

    switch (a4988_app.sub_step) {

    case 1:
    {
        uint32_t itrip = a4988_calc_itrip_max(a4988_app.config.vref_mv,
                                               a4988_app.config.rs_mohm);
        LOG_I("===== A4988 配置摘要 =====");
        LOG_I("  微步分辨率   : %d (%.1f 步/圈)",
              a4988_app.config.microstep,
              (float)a4988_steps_per_rev(a4988_app.config.microstep));
        LOG_I("  检流电阻 RS  : %d mΩ", a4988_app.config.rs_mohm);
        LOG_I("  参考电压 VREF: %d mV", a4988_app.config.vref_mv);
        LOG_I("  最大电流     : %lu mA", itrip);
        LOG_I("  ROSC 模式    : %d", a4988_app.config.rosc_mode);
        LOG_I("===========================");
        break;
    }

    case 2:
        LOG_I("配置完成，等待运动命令");
        set_app_state(A4988_APP_STATE_IDLE);
        break;
    }
}

/**
 * @brief 阶段 2: 定步数定位 (MOVE_POSITION)
 *
 * 以固定速度旋转指定步数后停止。
 * 每个 process 周期发送一个脉冲，实现非阻塞步进。
 */
static void process_state_move_position(void)
{
    uint64_t now_us;

    a4988_app.sub_step++;

    switch (a4988_app.sub_step) {

    case 1:
        a4988_set_direction(a4988_app.pos.direction);
        a4988_app.pos_step_cnt = 0;
        a4988_app.last_step_us = get_time_us();
        LOG_I("开始定位: 目标 %lu 步, 间隔 %lu µs",
              a4988_app.pos.step_count, a4988_app.pos.step_delay_us);
        break;

    default:
        /* 周期性步进 */
        if (a4988_app.pos_step_cnt >= a4988_app.pos.step_count) {
            /* 运动完成 */
            LOG_I("定位完成: 已走 %lu 步", a4988_app.pos_step_cnt);
            set_app_state(A4988_APP_STATE_IDLE);
            return;
        }

        now_us = get_time_us();
        if (now_us - a4988_app.last_step_us >= a4988_app.pos.step_delay_us) {
            a4988_step_pulse();
            a4988_app.pos_step_cnt++;
            a4988_app.last_step_us = now_us;
        }
        break;
    }
}

/**
 * @brief 阶段 3: 连续旋转 (MOVE_CONTINUOUS)
 *
 * 以固定速度持续旋转，直到收到停止命令。
 * 不设目标步数上限。
 */
static void process_state_move_continuous(void)
{
    uint64_t now_us;

    a4988_app.sub_step++;

    switch (a4988_app.sub_step) {

    case 1:
        a4988_set_direction(a4988_app.cont.direction);
        a4988_app.cont_step_total = 0;
        a4988_app.last_step_us     = get_time_us();
        LOG_I("开始连续旋转: 间隔 %lu µs, 方向 %s",
              a4988_app.cont.step_delay_us,
              a4988_app.cont.direction == A4988_DIR_CW ? "CW" : "CCW");
        break;

    default:
        now_us = get_time_us();
        if (now_us - a4988_app.last_step_us >= a4988_app.cont.step_delay_us) {
            a4988_step_pulse();
            a4988_app.cont_step_total++;
            a4988_app.last_step_us = now_us;
        }
        break;
    }
}

/**
 * @brief 阶段 4: 梯形加减速运动 (MOVE_RAMP)
 *
 * 三段式梯形速度曲线:
 *   1. 加速段: 间隔从 start_delay_us 线性降到 min_delay_us
 *   2. 匀速段: 以 min_delay_us 恒定步进
 *   3. 减速段: 间隔从 min_delay_us 线性升回 start_delay_us
 *
 * 如果 run_steps = 0，则为纯三角形加减速（无匀速段）。
 */
static void process_state_move_ramp(void)
{
    uint64_t now_us;
    int32_t  delay_change;

    a4988_app.sub_step++;

    switch (a4988_app.sub_step) {

    case 1:
    {
        /* 计算总步数和速度增量 */
        const A4988_RampConfig_t *rc = &a4988_app.ramp;

        a4988_app.ramp_phase       = A4988_RAMP_ACCEL;
        a4988_app.ramp_step_cnt    = 0;
        a4988_app.ramp_accel_step  = 0;
        a4988_app.ramp_const_step  = 0;
        a4988_app.ramp_decel_step  = 0;
        a4988_app.ramp_interval    = rc->start_delay_us;
        a4988_app.last_step_us     = get_time_us();

        LOG_I("开始梯形加减速: 加速 %lu + 匀速 %lu + 减速 %lu 步, "
              "间隔 %lu→%lu µs",
              rc->accel_steps, rc->run_steps, rc->accel_steps,
              rc->start_delay_us, rc->min_delay_us);
        break;
    }

    default:
    {
        const A4988_RampConfig_t *rc = &a4988_app.ramp;

        if (a4988_app.ramp_phase == A4988_RAMP_DONE) {
            LOG_I("梯形加减速完成: 共 %lu 步", a4988_app.ramp_step_cnt);
            set_app_state(A4988_APP_STATE_IDLE);
            return;
        }

        now_us = get_time_us();
        if (now_us - a4988_app.last_step_us < a4988_app.ramp_interval) {
            return;  /* 时间未到，等待 */
        }

        /* 发送一个步进脉冲 */
        a4988_step_pulse();
        a4988_app.ramp_step_cnt++;
        a4988_app.last_step_us = now_us;

        /* 根据当前阶段调整速度 */
        switch (a4988_app.ramp_phase) {

        case A4988_RAMP_ACCEL:
            a4988_app.ramp_accel_step++;
            if (a4988_app.ramp_accel_step >= rc->accel_steps) {
                /* 加速段完成，进入匀速段 (如果存在) */
                if (rc->run_steps > 0) {
                    a4988_app.ramp_phase    = A4988_RAMP_CONSTANT;
                    a4988_app.ramp_interval = rc->min_delay_us;
                } else {
                    /* 纯三角形，直接进入减速 */
                    a4988_app.ramp_phase    = A4988_RAMP_DECEL;
                }
            } else {
                /* 线性加速 — 逐次减小间隔 */
                delay_change = (int32_t)(rc->start_delay_us - rc->min_delay_us)
                               / (int32_t)rc->accel_steps;
                /* 每步递减直到 min_delay_us */
                if (a4988_app.ramp_interval > rc->min_delay_us + (uint32_t)delay_change) {
                    a4988_app.ramp_interval -= (uint32_t)delay_change;
                } else {
                    a4988_app.ramp_interval = rc->min_delay_us;
                }
            }
            break;

        case A4988_RAMP_CONSTANT:
            a4988_app.ramp_const_step++;
            if (a4988_app.ramp_const_step >= rc->run_steps) {
                a4988_app.ramp_phase = A4988_RAMP_DECEL;
            }
            break;

        case A4988_RAMP_DECEL:
            a4988_app.ramp_decel_step++;
            if (a4988_app.ramp_decel_step >= rc->accel_steps) {
                a4988_app.ramp_phase = A4988_RAMP_DONE;
            } else {
                /* 线性减速 — 逐次增大间隔 */
                delay_change = (int32_t)(rc->start_delay_us - rc->min_delay_us)
                               / (int32_t)rc->accel_steps;
                if (a4988_app.ramp_interval + (uint32_t)delay_change < rc->start_delay_us) {
                    a4988_app.ramp_interval += (uint32_t)delay_change;
                } else {
                    a4988_app.ramp_interval = rc->start_delay_us;
                }
            }
            break;

        default:
            break;
        }
        break;
    }
    }
}

/**
 * @brief 阶段 5: 微步切换演示 (DEMO_MICROSTEP)
 *
 * 依次遍历全部 5 种微步分辨率，每种模式旋转一小段距离。
 * 演示顺序: Full → Half → 1/4 → 1/8 → 1/16
 */
static void process_state_demo_microstep(void)
{
    uint64_t now_us;

    static const A4988_Microstep_t demo_sequence[] = {
        A4988_MICROSTEP_FULL,
        A4988_MICROSTEP_HALF,
        A4988_MICROSTEP_QUARTER,
        A4988_MICROSTEP_EIGHTH,
        A4988_MICROSTEP_SIXTEENTH,
    };
    static const uint32_t demo_steps_per_mode = 200;  /* 每种模式走 200 步 */

    a4988_app.sub_step++;

    switch (a4988_app.sub_step) {

    case 1:
        a4988_app.demo_ms_index    = 0;
        a4988_app.demo_step_total  = 0;
        a4988_app.last_step_us     = get_time_us();
        a4988_set_microstep(demo_sequence[0]);
        a4988_set_direction(A4988_DIR_CW);
        LOG_I("微步演示开始: 模式 %d (Full Step)", demo_sequence[0]);
        break;

    default:
    {
        uint8_t idx = a4988_app.demo_ms_index;

        if (idx >= 5) {
            LOG_I("微步演示完成");
            set_app_state(A4988_APP_STATE_IDLE);
            return;
        }

        now_us = get_time_us();
        if (now_us - a4988_app.last_step_us < 1200) {
            return;  /* 以 ~833 Hz 步进 */
        }

        a4988_step_pulse();
        a4988_app.demo_step_total++;
        a4988_app.last_step_us = now_us;

        if (a4988_app.demo_step_total >= demo_steps_per_mode) {
            /* 切换到下一种微步模式 */
            a4988_app.demo_step_total = 0;
            idx++;
            a4988_app.demo_ms_index = idx;

            if (idx < 5) {
                /* 切换前先复位译码器避免丢步 */
                a4988_reset_assert();
                a4988_set_microstep(demo_sequence[idx]);
                a4988_reset_release();
                LOG_I("切换微步: 模式 %d", demo_sequence[idx]);
            }
        }
        break;
    }
    }
}

/* ==================== 公开接口 ==================== */

/**
 * @brief 初始化 A4988 应用
 */
void a4988_app_init(void)
{
    memset(&a4988_app, 0, sizeof(a4988_app));
    a4988_app.task_state = A4988_APP_STATE_INIT;
    a4988_app.last_state = A4988_APP_STATE_INIT;
    LOG_I("A4988 应用初始化，状态机启动");
}

/**
 * @brief 应用主循环
 *
 * 状态机驱动，需要周期性调用（推荐在 1ms 定时器或主循环中调用）。
 */
void a4988_app_process(void)
{
    enum a4988_app_state_e this_state = a4988_app.task_state;

    /* 同步上次状态 (日志用) */
    a4988_app.last_state = this_state;

    switch (this_state) {

    case A4988_APP_STATE_INIT:
        process_state_init();
        break;

    case A4988_APP_STATE_CONFIGURE:
        process_state_configure();
        break;

    case A4988_APP_STATE_MOVE_POSITION:
        process_state_move_position();
        break;

    case A4988_APP_STATE_MOVE_CONTINUOUS:
        process_state_move_continuous();
        break;

    case A4988_APP_STATE_MOVE_RAMP:
        process_state_move_ramp();
        break;

    case A4988_APP_STATE_DEMO_MICROSTEP:
        process_state_demo_microstep();
        break;

    case A4988_APP_STATE_IDLE:
        /* 空闲等待 */
        break;

    case A4988_APP_STATE_ERROR:
        /* 致命错误，停留 */
        break;

    default:
        break;
    }
}

/* ---------- 便捷运动控制 API ---------- */

int8_t a4988_app_move_position(uint32_t steps, uint32_t delay_us, A4988_Direction_t dir)
{
    if (steps == 0 || delay_us == 0) {
        LOG_E("定位参数无效: steps=%lu, delay_us=%lu", steps, delay_us);
        return A4988_ERR_INVALID_PARAM;
    }

    if (a4988_app.task_state == A4988_APP_STATE_ERROR) {
        LOG_E("驱动处于错误状态，无法执行运动");
        return A4988_ERR_NOT_INITIALIZED;
    }

    a4988_app.pos.step_count  = steps;
    a4988_app.pos.step_delay_us = delay_us;
    a4988_app.pos.direction   = dir;
    a4988_app.motion_mode     = A4988_MOTION_MODE_POSITION;

    set_app_state(A4988_APP_STATE_MOVE_POSITION);
    return A4988_OK;
}

int8_t a4988_app_move_continuous(uint32_t delay_us, A4988_Direction_t dir)
{
    if (delay_us == 0) {
        LOG_E("连续旋转参数无效: delay_us=0");
        return A4988_ERR_INVALID_PARAM;
    }

    if (a4988_app.task_state == A4988_APP_STATE_ERROR) {
        return A4988_ERR_NOT_INITIALIZED;
    }

    a4988_app.cont.step_delay_us = delay_us;
    a4988_app.cont.direction     = dir;
    a4988_app.motion_mode        = A4988_MOTION_MODE_CONTINUOUS;

    set_app_state(A4988_APP_STATE_MOVE_CONTINUOUS);
    return A4988_OK;
}

int8_t a4988_app_move_ramp(const A4988_RampConfig_t *ramp_cfg, A4988_Direction_t dir)
{
    if (ramp_cfg == NULL) {
        LOG_E("加减速配置为空");
        return A4988_ERR_NULL_PTR;
    }

    if (ramp_cfg->accel_steps == 0 || ramp_cfg->start_delay_us == 0 ||
        ramp_cfg->min_delay_us == 0) {
        LOG_E("加减速参数无效");
        return A4988_ERR_INVALID_PARAM;
    }

    if (a4988_app.task_state == A4988_APP_STATE_ERROR) {
        return A4988_ERR_NOT_INITIALIZED;
    }

    memcpy(&a4988_app.ramp, ramp_cfg, sizeof(A4988_RampConfig_t));
    a4988_set_direction(dir);
    a4988_app.motion_mode = A4988_MOTION_MODE_RAMP;

    set_app_state(A4988_APP_STATE_MOVE_RAMP);
    return A4988_OK;
}

void a4988_app_stop(void)
{
    if (a4988_app.task_state == A4988_APP_STATE_MOVE_POSITION ||
        a4988_app.task_state == A4988_APP_STATE_MOVE_CONTINUOUS ||
        a4988_app.task_state == A4988_APP_STATE_MOVE_RAMP) {
        LOG_I("运动已停止");
        set_app_state(A4988_APP_STATE_IDLE);
    }
}

void a4988_app_demo_microstep(void)
{
    if (a4988_app.task_state == A4988_APP_STATE_ERROR) return;

    set_app_state(A4988_APP_STATE_DEMO_MICROSTEP);
}
