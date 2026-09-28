/**
* @file ina226_app.c
* @brief Platform-independent INA226 监测状态机示例实现
*
* 所属模块：INA226 驱动示例（状态机 + 统计 + 软 I2C）。
* 对应头文件：ina226_app.h；底层依赖：ina226.h + soft_i2c.h。
*
* 状态机完整迁移：
*   INIT ──(ina226_init)──→ CHECK_ID ──(ina226_read_device_info)──→
*   CONFIGURE ──(ina226_configure + calibrate + init_stats)──→
*   MONITOR ──(ina226_read_all + update_stats + check_thresholds)──→
*     ├─ 无阈值触发 → 留在 MONITOR
*     └─ 阈值触发 → CHECK_ALERT ──(configure_alert NONE)──→
*        DUMP ──(dump_registers)──→ IDLE（静态留在 IDLE）
*   任何步骤失败 → ERROR
*
* 统计更新：每次 read_all 后调 update_stats（min/max/sum/count/overflow）。
* 阈值检查：4 项独立（bus_ov / bus_uv / current_oc / power_ov），任何一项触发 → alert_count++ → 进入 CHECK_ALERT。
*/
#include "ina226_app.h"
#include "soft_i2c.h"

#include <stddef.h>
#include <string.h>

/* ===== 默认参数 ===== */
#define INA226_APP_DEFAULT_SAMPLE_INTERVAL_MS 200U   /**< MONITOR 默认采样间隔（ms） */

/* ===== 内部辅助函数 ===== */

/**
 * @brief  触发状态变更通知（notify 回调非 NULL 时调用）
 *
 * @param app  应用实例
 * @param message  变更描述字符串
 */
static void notify_state(ina226_app_t *app, const char *message)
{
    if ((app != NULL) && (app->cfg.notify != NULL)) {
        app->cfg.notify(app->state, message, app->cfg.notify_user);
    }
}

/**
 * @brief  迁移到新状态（记录 last_state → state，触发 notify）
 *
 * @param app  应用实例
 * @param state  新状态
 * @param message  变更描述字符串
 */
static void set_state(ina226_app_t *app,
                      ina226_app_state_t state,
                      const char *message)
{
    app->last_state = app->state;
    app->state = state;
    notify_state(app, message);
}

/**
 * @brief  初始化统计数据（min 字段置大值，其余清零）
 */
static void init_stats(ina226_app_stats_t *stats)
{
    memset(stats, 0, sizeof(*stats));
    stats->vbus_min_V = 1000000.0f;     /**< 置大值，首次采样即覆盖 */
    stats->current_min_A = 1000000.0f;
}

/**
 * @brief  用一次测量结果更新统计
 *
 * 更新项：min/max（4 项）、sum（3 项）、sample_count++、overflow_count（MASK_ENABLE OVF 置位时）。
 */
static void update_stats(ina226_app_stats_t *stats,
                         const ina226_meas_result_t *meas)
{
    if (meas->bus_voltage_V > stats->vbus_max_V) {
        stats->vbus_max_V = meas->bus_voltage_V;
    }
    if (meas->bus_voltage_V < stats->vbus_min_V) {
        stats->vbus_min_V = meas->bus_voltage_V;
    }
    if (meas->current_A > stats->current_max_A) {
        stats->current_max_A = meas->current_A;
    }
    if (meas->current_A < stats->current_min_A) {
        stats->current_min_A = meas->current_A;
    }
    if (meas->power_W > stats->power_max_W) {
        stats->power_max_W = meas->power_W;
    }

    stats->vbus_sum_V += meas->bus_voltage_V;
    stats->current_sum_A += meas->current_A;
    stats->power_sum_W += meas->power_W;
    stats->sample_count++;

    if (meas->math_overflow != 0U) {
        stats->overflow_count++;
    }
}

/**
 * @brief  检查阈值，返回是否有故障
 *
 * 4 项独立检查：bus_over / bus_under / current_over / power_over。
 * 阈值为 0 表示禁用该项。任何一项触发 → alert_count++ → 返回 1。
 * 触发标志存入 stats（bus_ov_triggered / bus_uv_triggered / current_oc_triggered / power_ov_triggered）。
 *
 * @param app  应用实例
 * @return 0=正常；1=有阈值触发
 */
static uint8_t check_thresholds(ina226_app_t *app)
{
    uint8_t fault = 0U;
    ina226_app_stats_t *stats = &app->stats;
    const ina226_meas_result_t *meas = &app->last_meas;

    if ((app->cfg.bus_over_voltage_V > 0.0f) &&
        (meas->bus_voltage_V > app->cfg.bus_over_voltage_V)) {
        stats->bus_ov_triggered = 1U;
        fault = 1U;
    }
    if ((app->cfg.bus_under_voltage_V > 0.0f) &&
        (meas->bus_voltage_V < app->cfg.bus_under_voltage_V)) {
        stats->bus_uv_triggered = 1U;
        fault = 1U;
    }
    if ((app->cfg.current_over_A > 0.0f) &&
        (meas->current_A > app->cfg.current_over_A)) {
        stats->current_oc_triggered = 1U;
        fault = 1U;
    }
    if ((app->cfg.power_over_W > 0.0f) &&
        (meas->power_W > app->cfg.power_over_W)) {
        stats->power_ov_triggered = 1U;
        fault = 1U;
    }

    if (fault != 0U) {
        stats->alert_count++;
    }
    return fault;
}

/* ===== 状态机处理函数 ===== */

/** @brief INIT：调用 ina226_init 绑定 ops + 校验地址 */
static ina226_ret_t process_init(ina226_app_t *app)
{
    ina226_ret_t ret = ina226_init(&app->dev,
                                   app->cfg.ops,
                                   app->cfg.ops_user,
                                   app->cfg.address);
    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "ina226 init failed");
        return ret;
    }

    set_state(app, INA226_APP_STATE_CHECK_ID, "init complete");
    return INA226_OK;
}

/** @brief CHECK_ID：读 Manufacturer/Die ID（底层 init 已校验，这里重新读供外部查询） */
static ina226_ret_t process_check_id(ina226_app_t *app)
{
    ina226_ret_t ret = ina226_read_device_info(&app->dev, &app->device_info);

    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "read device info failed");
        return ret;
    }

    set_state(app, INA226_APP_STATE_CONFIGURE, "device id checked");
    return INA226_OK;
}

/**
 * @brief CONFIGURE：写 CONFIG 寄存器 + 校准 + 初始化统计
 *
 * 顺序：ina226_configure（CONFIG）→ ina226_calibrate（CALIBRATION）→ init_stats + 重置 last_sample_ms。
 */
static ina226_ret_t process_configure(ina226_app_t *app)
{
    ina226_ret_t ret = ina226_configure(&app->dev,
                                        app->cfg.avg,
                                        app->cfg.bus_ct,
                                        app->cfg.shunt_ct,
                                        app->cfg.mode);
    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "configure failed");
        return ret;
    }

    ret = ina226_calibrate(&app->dev,
                           app->cfg.r_shunt_ohm,
                           app->cfg.max_expected_current_A,
                           app->cfg.current_lsb_A,
                           &app->calibration);
    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "calibration failed");
        return ret;
    }

    init_stats(&app->stats);
    app->last_sample_ms = 0U;
    set_state(app, INA226_APP_STATE_MONITOR, "configuration complete");
    return INA226_OK;
}

/**
 * @brief MONITOR：周期性 read_all + 更新统计 + 阈值检查
 *
 * 节拍控制：get_ms 回调获取当前时间，间隔 < sample_interval_ms（默认 200ms）时直接返回 OK。
 * 无阈值触发 → 留在 MONITOR；有触发 → 进入 CHECK_ALERT。
 */
static ina226_ret_t process_monitor(ina226_app_t *app)
{
    uint32_t now;
    uint32_t interval = app->cfg.sample_interval_ms;
    ina226_ret_t ret;

    if (app->cfg.get_ms == NULL) {
        set_state(app, INA226_APP_STATE_ERROR, "missing time source");
        return INA226_RET_NULL;
    }

    if (interval == 0U) {
        interval = INA226_APP_DEFAULT_SAMPLE_INTERVAL_MS;
    }

    now = app->cfg.get_ms(app->cfg.time_user);
    /* 节流：不到间隔直接返回 */
    if ((app->last_sample_ms != 0U) &&
        ((uint32_t)(now - app->last_sample_ms) < interval)) {
        return INA226_OK;
    }
    app->last_sample_ms = now;

    /* 读全部测量值 */
    ret = ina226_read_all(&app->dev, &app->last_meas);
    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "measurement read failed");
        return ret;
    }

    update_stats(&app->stats, &app->last_meas);
    if (check_thresholds(app) != 0U) {
        set_state(app, INA226_APP_STATE_CHECK_ALERT, "threshold triggered");
    }

    return INA226_OK;
}

/** @brief CHECK_ALERT：清除硬件 Alert（写 MASK_ENABLE=0），然后进入 DUMP */
static ina226_ret_t process_check_alert(ina226_app_t *app)
{
    ina226_ret_t ret = ina226_configure_alert(&app->dev,
                                              INA226_ALERT_NONE,
                                              0U,
                                              INA226_ALERT_POLARITY_ACTIVE_LOW,
                                              INA226_ALERT_TRANSPARENT);
    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "alert clear failed");
        return ret;
    }

    set_state(app, INA226_APP_STATE_DUMP, "alert handled");
    return INA226_OK;
}

/** @brief DUMP：读全部 8 个寄存器到 dump_regs[]，然后进入 IDLE */
static ina226_ret_t process_dump(ina226_app_t *app)
{
    ina226_ret_t ret = ina226_dump_registers(&app->dev, app->dump_regs);

    if (ret != INA226_OK) {
        set_state(app, INA226_APP_STATE_ERROR, "dump failed");
        return ret;
    }

    set_state(app, INA226_APP_STATE_IDLE, "dump complete");
    return INA226_OK;
}

/* ===== 对外接口 ===== */

/**
 * @brief  初始化 INA226 监测应用
 *
 * 校验项：app/cfg 非 NULL、ops + get_ms 非 NULL、r_shunt_ohm/max_expected_current > 0。
 * 成功后 memset 清零 → 拷贝 cfg → state=INIT → initialized=1。
 */
ina226_ret_t ina226_app_init(ina226_app_t *app,
                             const ina226_app_config_t *cfg)
{
    if ((app == NULL) || (cfg == NULL)) {
        return INA226_RET_NULL;
    }
    if ((cfg->ops == NULL) || (cfg->ops->read_reg16 == NULL) ||
        (cfg->ops->write_reg16 == NULL) || (cfg->get_ms == NULL)) {
        return INA226_RET_NULL;
    }
    if ((cfg->r_shunt_ohm <= 0.0f) ||
        (cfg->max_expected_current_A <= 0.0f)) {
        return INA226_RET_PARAM;
    }

    memset(app, 0, sizeof(*app));
    app->cfg = *cfg;
    app->state = INA226_APP_STATE_INIT;
    app->last_state = INA226_APP_STATE_IDLE;
    app->initialized = 1U;
    return INA226_OK;
}

/**
 * @brief  执行一次状态机推进
 *
 * 按 state switch 分派。MONITOR 状态内部有节流逻辑（不到时间返回 OK，不迁移状态）。
 * IDLE 静态留在 IDLE（不做任何事）。ERROR/default 直接返回 RET_PARAM。
 */
ina226_ret_t ina226_app_process(ina226_app_t *app)
{
    if (app == NULL) {
        return INA226_RET_NULL;
    }
    if (app->initialized == 0U) {
        return INA226_RET_PARAM;
    }

    switch (app->state) {
    case INA226_APP_STATE_INIT:
        return process_init(app);
    case INA226_APP_STATE_CHECK_ID:
        return process_check_id(app);
    case INA226_APP_STATE_CONFIGURE:
        return process_configure(app);
    case INA226_APP_STATE_MONITOR:
        return process_monitor(app);
    case INA226_APP_STATE_CHECK_ALERT:
        return process_check_alert(app);
    case INA226_APP_STATE_DUMP:
        return process_dump(app);
    case INA226_APP_STATE_IDLE:
        return INA226_OK;                /**< IDLE 静态停留，不迁移 */
    case INA226_APP_STATE_ERROR:
    default:
        return INA226_RET_PARAM;
    }
}

/** @brief 查询当前状态（NULL 安全，返回 ERROR） */
ina226_app_state_t ina226_app_get_state(const ina226_app_t *app)
{
    if (app == NULL) {
        return INA226_APP_STATE_ERROR;
    }
    return app->state;
}

/* ===== 软 I2C 回调实现 ===== */

/**
 * @brief  软 I2C 读 16bit 寄存器
 *
 * 流程：写寄存器地址消息 + repeated START + 读 2 字节消息，INA226 数据为大端序。
 */
int ina226_soft_i2c_read_reg16(void *user,
                               uint8_t address,
                               uint8_t reg,
                               uint16_t *value)
{
    uint8_t command = reg;
    uint8_t data[2];
    soft_i2c_msg_t messages[2];
    soft_i2c_ret_t ret;

    if ((user == NULL) || (value == NULL)) {
        return -1;
    }

    messages[0].address = address;
    messages[0].flags = 0U;
    messages[0].buffer = &command;
    messages[0].length = 1U;
    messages[0].transferred = 0U;
    messages[0].trailing_bytes = 0U;
    messages[1].address = address;
    messages[1].flags = SOFT_I2C_MSG_READ;
    messages[1].buffer = data;
    messages[1].length = sizeof(data);
    messages[1].transferred = 0U;
    messages[1].trailing_bytes = 0U;

    ret = soft_i2c_transfer((soft_i2c_bus_t *)user, messages, 2U);
    if (ret != SOFT_I2C_OK) {
        return -1;
    }

    *value = (uint16_t)(((uint16_t)data[0] << 8) | data[1]);
    return 0;
}

/**
 * @brief  软 I2C 写 16bit 寄存器
 *
 * 流程：单条写消息依次发送寄存器地址、高字节、低字节。
 */
int ina226_soft_i2c_write_reg16(void *user,
                                uint8_t address,
                                uint8_t reg,
                                uint16_t value)
{
    uint8_t data[3];
    soft_i2c_msg_t message;
    soft_i2c_ret_t ret;

    if (user == NULL) {
        return -1;
    }

    data[0] = reg;
    data[1] = (uint8_t)(value >> 8U);
    data[2] = (uint8_t)value;
    message.address = address;
    message.flags = 0U;
    message.buffer = data;
    message.length = sizeof(data);
    message.transferred = 0U;
    message.trailing_bytes = 0U;

    ret = soft_i2c_transfer((soft_i2c_bus_t *)user, &message, 1U);
    return (ret == SOFT_I2C_OK) ? 0 : -1;
}
