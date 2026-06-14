/**
 * @file a4988_app.h
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief A4988 应用层 — 步进电机控制示例与状态机
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @details 提供以下应用示例：
 *          - 定步数定位 (绝对 / 相对位置控制)
 *          - 连续恒速旋转
 *          - 梯形加减速运动 (ramp)
 *          - 微步切换演示
 *
 *          应用层采用状态机驱动模式，周期性调用 process() 推进任务。
 */

#ifndef A4988_APP_H
#define A4988_APP_H

#include "a4988.h"

/* ======================== 应用状态机枚举 ======================== */

/**
 * @brief A4988 应用任务状态机
 */
enum a4988_app_state_e {

    /* --- 初始化阶段 --- */
    A4988_APP_STATE_INIT,           /**< 等待驱动初始化完成 */

    /* --- 配置阶段 --- */
    A4988_APP_STATE_CONFIGURE,      /**< 设置运行参数 */

    /* --- 运动控制阶段 --- */
    A4988_APP_STATE_MOVE_POSITION,  /**< 定步数定位运动 */
    A4988_APP_STATE_MOVE_CONTINUOUS,/**< 连续旋转 */
    A4988_APP_STATE_MOVE_RAMP,      /**< 梯形加减速运动 */

    /* --- 演示阶段 --- */
    A4988_APP_STATE_DEMO_MICROSTEP, /**< 微步切换演示 */

    /* --- 终态 --- */
    A4988_APP_STATE_IDLE,           /**< 任务完成，空闲 */
    A4988_APP_STATE_ERROR,          /**< 错误状态 */

    A4988_APP_STATE_COUNT
};

/* ======================== 运动模式枚举 ======================== */

/**
 * @brief 运动模式类型
 */
typedef enum {
    A4988_MOTION_MODE_POSITION   = 0,   /**< 定步数定位 */
    A4988_MOTION_MODE_CONTINUOUS = 1,   /**< 连续恒速旋转 */
    A4988_MOTION_MODE_RAMP       = 2,   /**< 梯形加减速 */
} A4988_MotionMode_t;

/* ======================== 梯形加减速阶段 ======================== */

typedef enum {
    A4988_RAMP_ACCEL    = 0,    /**< 加速阶段 */
    A4988_RAMP_CONSTANT = 1,    /**< 匀速阶段 */
    A4988_RAMP_DECEL    = 2,    /**< 减速阶段 */
    A4988_RAMP_DONE     = 3,    /**< 运动完成 */
} A4988_RampPhase_t;

/* ======================== 运动参数结构体 ======================== */

/**
 * @brief 梯形加减速运动参数
 */
typedef struct {
    uint32_t accel_steps;       /**< 加速段步数 (也是减速段步数) */
    uint32_t run_steps;         /**< 匀速段步数 (0 表示纯三角形加减速) */
    uint32_t total_steps;       /**< 总步数 = 2 × accel + run */
    uint32_t start_delay_us;    /**< 起始步进间隔 (µs)，对应最低速度 */
    uint32_t min_delay_us;      /**< 最小时步进间隔 (µs)，对应最高速度 */
} A4988_RampConfig_t;

/**
 * @brief 定位运动参数
 */
typedef struct {
    uint32_t step_count;        /**< 目标步数 */
    uint32_t step_delay_us;     /**< 步进间隔 (µs) */
    A4988_Direction_t direction;/**< 运动方向 */
} A4988_PositionConfig_t;

/**
 * @brief 连续旋转参数
 */
typedef struct {
    uint32_t step_delay_us;     /**< 步进间隔 (µs)，决定转速 */
    A4988_Direction_t direction;/**< 旋转方向 */
} A4988_ContinuousConfig_t;

/* ======================== 应用上下文结构体 ======================== */

typedef void (*a4988_app_notify_cb)(uint8_t event, void *arg);

/**
 * @brief A4988 应用全局上下文
 */
struct a4988_app_t {

    /* --- 状态机 --- */
    enum a4988_app_state_e task_state;      /**< 当前任务状态 */
    enum a4988_app_state_e last_state;      /**< 上次任务状态 */
    uint8_t                sub_step;        /**< 当前状态的子步骤 */

    /* --- 回调 --- */
    a4988_app_notify_cb    notify_cb;       /**< 事件通知回调 */

    /* --- 驱动配置 --- */
    A4988_Config_t         config;          /**< A4988 硬件配置 */

    /* --- 运动控制 --- */
    A4988_MotionMode_t     motion_mode;     /**< 当前运动模式 */

    /* 定位运动 */
    A4988_PositionConfig_t pos;             /**< 定位运动参数 */
    uint32_t               pos_step_cnt;    /**< 当前已走步数 */

    /* 连续旋转 */
    A4988_ContinuousConfig_t cont;          /**< 连续旋转参数 */
    uint32_t               cont_step_total; /**< 累计步数 */

    /* 梯形加减速 */
    A4988_RampConfig_t     ramp;            /**< 梯形加减速参数 */
    A4988_RampPhase_t      ramp_phase;      /**< 当前加减速阶段 */
    uint32_t               ramp_step_cnt;   /**< 当前已走步数 */
    uint32_t               ramp_interval;   /**< 当前步进间隔 (µs) */
    uint32_t               ramp_accel_step; /**< 加速段内步数计数 */
    uint32_t               ramp_decel_step; /**< 减速段内步数计数 */
    uint32_t               ramp_const_step; /**< 匀速段内步数计数 */

    /* 微步演示 */
    uint8_t                demo_ms_index;   /**< 当前演示微步索引 */
    uint32_t               demo_step_total; /**< 演示步数累计 */

    /* 时间管理 */
    uint64_t               last_step_us;    /**< 上次步进时间戳 (µs) */
};

extern struct a4988_app_t a4988_app;

/* ======================== API 函数声明 ======================== */

/**
 * @brief 初始化 A4988 应用
 *
 * 使用默认配置初始化驱动，状态机进入 INIT 阶段。
 */
void a4988_app_init(void);

/**
 * @brief 应用主循环 — 需周期性调用以驱动状态机
 *
 * 处理当前状态的子步骤，推进状态机前进。
 * 调用频率应 ≥ 最高步进频率（例如 1kHz+）。
 */
void a4988_app_process(void);

/* ---------- 便捷运动控制 API ---------- */

/**
 * @brief 启动定步数定位运动
 *
 * 阻塞式 (调用后立即开始执行，在 process 中逐脉冲推进)。
 *
 * @param steps     目标步数
 * @param delay_us  步进间隔 (µs)
 * @param dir       方向
 * @return int8_t   A4988_OK 或错误码
 */
int8_t a4988_app_move_position(uint32_t steps, uint32_t delay_us, A4988_Direction_t dir);

/**
 * @brief 启动连续旋转
 *
 * @param delay_us  步进间隔 (µs)
 * @param dir       方向
 * @return int8_t   A4988_OK 或错误码
 */
int8_t a4988_app_move_continuous(uint32_t delay_us, A4988_Direction_t dir);

/**
 * @brief 启动梯形加减速运动
 *
 * @param ramp_cfg  加减速配置
 * @param dir       方向
 * @return int8_t   A4988_OK 或错误码
 */
int8_t a4988_app_move_ramp(const A4988_RampConfig_t *ramp_cfg, A4988_Direction_t dir);

/**
 * @brief 停止当前运动
 */
void a4988_app_stop(void);

/**
 * @brief 启动微步切换演示
 */
void a4988_app_demo_microstep(void);

#endif /* A4988_APP_H */
