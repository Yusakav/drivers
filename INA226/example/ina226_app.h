/**
* @file ina226_app.h
* @brief Platform-independent INA226 监测状态机示例对外接口声明
*
* 所属模块：INA226 驱动示例（演示状态机 + 软 I2C 回调 + 阈值保护 + 统计）。
* 对应实现：ina226_app.c；底层依赖：ina226.h + soft_i2c.h。
*
* 状态机流程（7 状态 + 1 错误）：
*   INIT → CHECK_ID → CONFIGURE → MONITOR ⇌ CHECK_ALERT → DUMP → IDLE
*                                ↑                              ↓
*                                └────── 任何步骤失败 → ERROR ←┘
*
* 软 I2C 回调：ina226_soft_i2c_read_reg16 / ina226_soft_i2c_write_reg16
*   基于 soft_i2c_bus_t + soft_i2c_transfer，GPIO bit-bang 实现。
*   传入 ina226_app_config_t.ops_user = &soft_i2c_bus。
*
* 使用约定：
*   1. 初始化 soft_i2c_bus_t（GPIO + 延时）；
*   2. 构造 ina226_app_config_t（ops + get_ms + 阈值）；
*   3. ina226_app_init → while(1) ina226_app_process（主循环节拍）；
*   4. 任何时刻调 ina226_app_get_state 查询状态。
*/
#ifndef INA226_APP_H
#define INA226_APP_H

#include <stdint.h>
#include "ina226.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 状态机状态 ===== */
typedef enum {
    INA226_APP_STATE_INIT = 0,        /**< 初始化 ina226_t */
    INA226_APP_STATE_CHECK_ID,        /**< 读 Manufacturer/Die ID 校验 */
    INA226_APP_STATE_CONFIGURE,       /**< 写 CONFIG + CALIBRATION */
    INA226_APP_STATE_MONITOR,         /**< 周期性 read_all + 阈值检查 */
    INA226_APP_STATE_CHECK_ALERT,     /**< 阈值触发 → 清除 Alert */
    INA226_APP_STATE_DUMP,            /**< 阈值触发 → dump 全部寄存器 */
    INA226_APP_STATE_IDLE,            /**< 等待（可再次触发监控） */
    INA226_APP_STATE_ERROR,           /**< 错误（任何步骤失败） */
} ina226_app_state_t;

/* ===== 回调类型 ===== */

/** @brief 获取毫秒时间戳回调（MONITOR 状态节拍依赖，必须实现） */
typedef uint32_t (*ina226_app_get_ms_t)(void *user);

/** @brief 状态变更通知回调（可选，state 从 last_state 迁移时触发） */
typedef void (*ina226_app_notify_t)(ina226_app_state_t state,
                                    const char *message,
                                    void *user);

/* ===== 配置结构体 ===== */
typedef struct {
    /* —— I2C —— */
    const ina226_i2c_ops_t *ops;       /**< I2C 操作回调表（read_reg16/write_reg16，必须非 NULL） */
    void *ops_user;                     /**< I2C 回调共用用户参数（通常是 soft_i2c_bus_t*） */
    uint8_t address;                    /**< INA226 I2C 地址（0x40 ~ 0x4F） */

    /* —— 校准参数 —— */
    float r_shunt_ohm;                  /**< 分流电阻值（Ω，必须 > 0） */
    float max_expected_current_A;       /**< 最大期望电流（A，必须 > 0） */
    float current_lsb_A;               /**< 期望电流 LSB（A/step，0=自动计算） */

    /* —— CONFIG 参数 —— */
    ina226_avg_t avg;                   /**< 均值模式 */
    ina226_conv_time_t bus_ct;          /**< 总线电压转换时间 */
    ina226_conv_time_t shunt_ct;        /**< 分流电压转换时间 */
    ina226_mode_t mode;                 /**< 工作模式（通常 SHUNT_BUS_CONT） */

    /* —— 阈值保护 —— */
    float bus_over_voltage_V;           /**< 总线过压阈值（V，0=禁用） */
    float bus_under_voltage_V;          /**< 总线欠压阈值（V，0=禁用） */
    float current_over_A;               /**< 过流阈值（A，0=禁用） */
    float power_over_W;                 /**< 功率超限阈值（W，0=禁用） */
    uint32_t sample_interval_ms;        /**< 采样间隔（ms，0=默认 200ms） */

    /* —— 回调 —— */
    ina226_app_get_ms_t get_ms;         /**< 时间获取回调（必须非 NULL） */
    void *time_user;                    /**< get_ms 回调参数 */
    ina226_app_notify_t notify;         /**< 状态变更通知回调（可选） */
    void *notify_user;                  /**< notify 回调参数 */
} ina226_app_config_t;

/* ===== 统计结构体 ===== */
typedef struct {
    float vbus_max_V;                 /**< 总线电压最大值（V） */
    float vbus_min_V;                 /**< 总线电压最小值（V） */
    float vbus_sum_V;                 /**< 总线电压累计和（用于平均） */
    float current_max_A;              /**< 电流最大值（A） */
    float current_min_A;              /**< 电流最小值（A） */
    float current_sum_A;              /**< 电流累计和 */
    float power_max_W;                /**< 功率最大值（W） */
    float power_sum_W;                /**< 功率累计和 */
    uint32_t sample_count;            /**< 采样次数 */
    uint32_t alert_count;             /**< 阈值触发次数 */
    uint32_t overflow_count;          /**< MASK_ENABLE OVF 置位次数 */
    uint8_t bus_ov_triggered;         /**< 总线过压曾触发 */
    uint8_t bus_uv_triggered;         /**< 总线欠压曾触发 */
    uint8_t current_oc_triggered;     /**< 过流曾触发 */
    uint8_t power_ov_triggered;       /**< 功率超限曾触发 */
} ina226_app_stats_t;

/* ===== 应用实例结构体 ===== */
typedef struct {
    ina226_t dev;                      /**< INA226 底层驱动实例 */
    ina226_app_config_t cfg;           /**< 配置（init 时拷贝） */
    ina226_app_state_t state;          /**< 当前状态 */
    ina226_app_state_t last_state;     /**< 上一状态（set_state 记录） */
    ina226_app_stats_t stats;          /**< 统计数据 */
    ina226_meas_result_t last_meas;    /**< 最近一次测量结果（MONITOR 写入） */
    ina226_device_info_t device_info;  /**< CHECK_ID 读出的芯片 ID */
    ina226_calibration_t calibration;  /**< calibrate 输出（含 LSB） */
    uint16_t dump_regs[8];             /**< DUMP 状态读出的 8 个寄存器 */
    uint32_t last_sample_ms;           /**< 上次采样时间戳（MONITOR 节拍控制） */
    uint8_t initialized;               /**< app_init 完成标志 */
} ina226_app_t;

/* ===== 对外接口 ===== */

/**
 * @brief  初始化 INA226 监测应用
 *
 * 参数校验：app/cfg 非 NULL、ops/get_ms 非 NULL、r_shunt_ohm/max_expected_current > 0。
 * 成功后 memset 清零 app、拷贝 cfg、state=INIT、initialized=1。
 *
 * @param app  输出应用实例（调用者分配）
 * @param cfg  初始化配置
 * @return INA226_OK / RET_NULL / RET_PARAM
 */
ina226_ret_t ina226_app_init(ina226_app_t *app,
                             const ina226_app_config_t *cfg);

/**
 * @brief  执行一次状态机推进（主循环节拍调用）
 *
 * 按 state 分派到 process_init / check_id / configure / monitor / check_alert / dump。
 * MONITOR 状态按 sample_interval_ms 节流（不到时间直接返回 OK）。
 * 任何步骤失败 → state=ERROR 并调 notify。
 *
 * @param app  已初始化的应用实例
 * @return INA226_OK / RET_NULL / RET_PARAM / 底层 INA226 错误码
 */
ina226_ret_t ina226_app_process(ina226_app_t *app);

/**
 * @brief  查询当前状态
 * @param app  应用实例
 * @return ina226_app_state_t（NULL 返回 ERROR）
 */
ina226_app_state_t ina226_app_get_state(const ina226_app_t *app);

/* ===== 软 I2C 回调实现（基于 soft_i2c.h） ===== */

/**
 * @brief  软 I2C 读 16bit 寄存器（可赋给 ina226_app_config_t.ops.read_reg16）
 * @param user  soft_i2c_bus_t*
 * @param address  I2C 从地址
 * @param reg  寄存器地址
 * @param value  输出寄存器值（大端序 data[0]<<8 | data[1]）
 * @return 0=成功；-1=失败
 */
int ina226_soft_i2c_read_reg16(void *user,
                               uint8_t address,
                               uint8_t reg,
                               uint16_t *value);

/**
 * @brief  软 I2C 写 16bit 寄存器（可赋给 ina226_app_config_t.ops.write_reg16）
 * @param user  soft_i2c_bus_t*
 * @param address  I2C 从地址
 * @param reg  寄存器地址
 * @param value  写入寄存器值（大端序 data[0]=高字节）
 * @return 0=成功；-1=失败
 */
int ina226_soft_i2c_write_reg16(void *user,
                                uint8_t address,
                                uint8_t reg,
                                uint16_t value);

#ifdef __cplusplus
}
#endif

#endif /* INA226_APP_H */
