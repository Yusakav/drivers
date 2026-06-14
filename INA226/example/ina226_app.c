/**
 * @file ina226_app.c
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief INA226 应用层 — 状态机驱动电源监控
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @details 模拟电池管理系统 (BMS) 典型监控流程:
 *          INIT → CHECK_ID → CONFIGURE → MONITOR (循环)
 *                                         ├→ CHECK_ALERT (报警子状态)
 *                                         └→ DUMP (触发转储)
 *
 *          硬件抽象层通过 soft_i2c_bus_t 回调结构体实现 GPIO 解耦:
 *            - set_scl / set_sda / get_scl / get_sda / delay_us
 */

#include "ina226_app.h"
#include "ina226.h"
#include "soft_i2c.h"
#include <string.h>

/* ==================== 内部常量 ==================== */

/** @brief 超时阈值 (ms) */
#define TIMEOUT_INIT_MS         5000
#define TIMEOUT_CHECK_ID_MS     1000
#define TIMEOUT_CONFIGURE_MS    3000
#define TIMEOUT_MONITOR_MS      60000
#define TIMEOUT_DUMP_MS         2000
#define TIMEOUT_DEFAULT_MS      5000

/** @brief MONITOR 状态采样间隔 (ms) */
#define MONITOR_SAMPLE_INTERVAL_MS  200

/** @brief DUMP 状态转储间隔 (ms) */
#define DUMP_INTERVAL_MS            5000

/** @brief 最大监控采样次数 (防止 32-bit 溢出) */
#define MONITOR_MAX_SAMPLES         10000000UL

/**
 * @brief 状态机名称字符串表
 */
static const char *s_state_names[] = {
    [INA226_STATE_INIT]       = "INIT",
    [INA226_STATE_CHECK_ID]   = "CHECK_ID",
    [INA226_STATE_CONFIGURE]  = "CONFIGURE",
    [INA226_STATE_MONITOR]    = "MONITOR",
    [INA226_STATE_CHECK_ALERT]= "CHECK_ALERT",
    [INA226_STATE_DUMP]       = "DUMP",
    [INA226_STATE_IDLE]       = "IDLE",
    [INA226_STATE_TIMEOUT]    = "TIMEOUT",
    [INA226_STATE_ERROR]      = "ERROR",
};

/* ==================== I2C 总线硬件抽象实例 ==================== */

/**
 * @brief 微秒级延时 (使用系统 tick)
 */
static void app_delay_us(uint32_t us)
{
    /* 占位: 替换为实际平台的 μs 延时函数   */
    /* 如 SysTick / DWT 周期计数器           */
    volatile uint32_t i;
    (void)us;
    /* 简单 busy-wait 示例 (需根据主频调整) */
    for (i = 0; i < us * 8; i++) {
        __NOP();
    }
}

/** @brief SCL 输出 */
static void app_set_scl(uint8_t level)
{
    if (level) INA226_SCL_H(); else INA226_SCL_L();
}

/** @brief SDA 输出 */
static void app_set_sda(uint8_t level)
{
    if (level) INA226_SDA_H(); else INA226_SDA_L();
}

/** @brief SCL 输入 */
static uint8_t app_get_scl(void)
{
    return INA226_SCL_READ();
}

/** @brief SDA 输入 */
static uint8_t app_get_sda(void)
{
    return INA226_SDA_READ();
}

/**
 * @brief INA226 专用 I2C 总线实例
 *
 * half_period_us = 5 → 约 100kHz SCL
 * timeout_us     = 30000 → SMBus 超时 30ms (INA226 要求 < 28ms 需关断，但此处为 I2C 层超时)
 */
static struct soft_i2c_bus_t s_ina226_i2c_bus = {
    .set_scl        = app_set_scl,
    .set_sda        = app_set_sda,
    .get_scl        = app_get_scl,
    .get_sda        = app_get_sda,
    .delay_us       = app_delay_us,
    .half_period_us = 5,
    .timeout_us     = 30000,
};

/* ==================== 全局应用实例 ==================== */

static ina226_app_t *s_app = NULL;

/* ==================== 状态管理辅助函数 ==================== */

/**
 * @brief 设置状态机超时
 */
static void set_state_machine_timeout(ina226_app_t *app, uint32_t timeout_ms)
{
    app->timeout_ms       = timeout_ms;
    app->timeout_start_ms = app->last_now_ms;
}

/**
 * @brief 设置状态机状态并通知回调
 */
static void set_state_machine_state(ina226_app_t *app,
                                     ina226_app_state_t state,
                                     const char *reason)
{
    app->last_state = app->task_state;
    app->task_state = state;
    app->sub_step   = 0;
    set_state_machine_timeout(app, TIMEOUT_DEFAULT_MS);

    if (app->notify_handler) {
        char msg[128];
        snprintf(msg, sizeof(msg), "State -> %s: %s",
                 s_state_names[state], reason ? reason : "");
        app->notify_handler(app->task_state, 0, app->notify_arg, msg);
    }
}

/**
 * @brief 进入错误状态
 */
static void enter_error_state(ina226_app_t *app, const char *reason)
{
    set_state_machine_state(app, INA226_STATE_ERROR, reason);
}

/* ==================== 状态处理函数 ==================== */

/**
 * @brief INIT 状态: GPIO 初始化 + I2C 扫描 + 驱动初始化
 */
static void process_state_init(ina226_app_t *app)
{
    int8_t  ret;
    uint8_t found = 0;

    switch (app->sub_step) {
    case 0:
        LOG_I("初始化 INA226 应用 ...");

        /* 设置 GPIO 为开漏输出模式 (占位，替换为实际 GPIO API) */
        /* GPIO_ModeCfg(INA226_PIN_SCL_PORT, INA226_PIN_SCL_PIN, GPIO_ModeOut_OD); */
        /* GPIO_ModeCfg(INA226_PIN_SDA_PORT, INA226_PIN_SDA_PIN, GPIO_ModeOut_OD); */
        /* GPIO_ModeCfg(INA226_PIN_ALERT_PORT, INA226_PIN_ALERT_PIN, GPIO_ModeIN_PU); */
        INA226_SCL_H();
        INA226_SDA_H();

        set_state_machine_timeout(app, TIMEOUT_INIT_MS);
        app->sub_step = 1;
        break;

    case 1:
        /* I2C 总线扫描 */
        i2c_scan(&s_ina226_i2c_bus, I2C_M_7BIT | I2C_M_DATA_16BIT | I2C_M_REG_8BIT);
        found = 1; /* scan 函数不返回结果，这里手动标记 */
        app->sub_step = 2;
        break;

    case 2:
        /* 初始化 INA226 驱动 (含 ID 校验) */
        ret = ina226_init(&s_ina226_i2c_bus, INA226_ADDR_A0_GND_A1_GND);

        if (ret != INA226_OK) {
            LOG_E("驱动初始化失败 (ret=%d)", ret);
            enter_error_state(app, "驱动初始化失败");
            return;
        }

        LOG_I("INA226 驱动初始化成功，跳转到 CHECK_ID");
        set_state_machine_state(app, INA226_STATE_CHECK_ID, "INIT 完成");
        break;

    default:
        enter_error_state(app, "INIT sub_step 越界");
        break;
    }
}

/**
 * @brief CHECK_ID 状态: 器件 ID 二次校验 + 器件信息打印
 */
static void process_state_check_id(ina226_app_t *app)
{
    ina226_device_info_t info;
    int8_t ret;

    switch (app->sub_step) {
    case 0:
        set_state_machine_timeout(app, TIMEOUT_CHECK_ID_MS);
        app->sub_step = 1;
        break;

    case 1:
        ret = ina226_read_device_info(&info);
        if (ret != INA226_OK) {
            LOG_E("读取器件信息失败 (ret=%d)", ret);
            enter_error_state(app, "读取器件信息失败");
            return;
        }

        LOG_I("INA226 器件信息:");
        LOG_I("  Manufacturer ID : 0x%04X", info.manufacturer_id);
        LOG_I("  Die ID          : 0x%04X", info.die_id);
        LOG_I("  Revision ID     : %u",     info.revision_id);

        set_state_machine_state(app, INA226_STATE_CONFIGURE, "CHECK_ID 完成");
        break;

    default:
        enter_error_state(app, "CHECK_ID sub_step 越界");
        break;
    }
}

/**
 * @brief CONFIGURE 状态: 配置采样参数 + 校准
 */
static void process_state_configure(ina226_app_t *app)
{
    int8_t               ret;
    ina226_calibration_t cal;

    switch (app->sub_step) {
    case 0:
        set_state_machine_timeout(app, TIMEOUT_CONFIGURE_MS);
        app->sub_step = 1;
        break;

    case 1:
        /* 配置: 16 次平均, 1.1ms 转换时间, 连续模式 (分流+总线) */
        LOG_I("配置 INA226: AVG=16, CT=1.1ms, Mode=Shunt+Bus Continuous");
        ret = ina226_configure(INA226_AVG_16,
                               INA226_CT_1100US,
                               INA226_CT_1100US,
                               INA226_MODE_SHUNT_BUS_CONT);
        if (ret != INA226_OK) {
            LOG_E("配置失败 (ret=%d)", ret);
            enter_error_state(app, "配置失败");
            return;
        }
        app->sub_step = 2;
        break;

    case 2:
        /* 校准 */
        LOG_I("校准: Rshunt=%.4fΩ, MaxI=%.3fA",
              INA226_APP_RSHUNT_OHM, INA226_APP_MAX_CURRENT_A);

        ret = ina226_calibrate(INA226_APP_RSHUNT_OHM,
                               INA226_APP_MAX_CURRENT_A,
                               &cal);
        if (ret != INA226_OK) {
            LOG_E("校准失败 (ret=%d)", ret);
            enter_error_state(app, "校准失败");
            return;
        }

        LOG_I("校准参数: CAL=0x%04X, I_LSB=%.6fA, P_LSB=%.6fW",
              cal.calibration_value, cal.current_lsb_A, cal.power_lsb_W);

        /* 初始化监控结果 */
        memset(&app->result, 0, sizeof(app->result));
        app->result.vbus_min_V = 999.0f; /* 初始设为极大值 */

        set_state_machine_state(app, INA226_STATE_MONITOR, "CONFIGURE 完成");
        break;

    default:
        enter_error_state(app, "CONFIGURE sub_step 越界");
        break;
    }
}

/**
 * @brief MONITOR 状态: 持续电源监控主循环
 *
 * 按 MONITOR_SAMPLE_INTERVAL_MS 间隔采样。
 * 每轮检查保护阈值，触发时跳转到 CHECK_ALERT。
 */
static void process_state_monitor(ina226_app_t *app)
{
    static uint32_t s_last_sample_ms = 0;
    ina226_meas_result_t meas;
    int8_t ret;

    switch (app->sub_step) {
    case 0:
        LOG_I("进入电源监控模式 (间隔=%ums)...", MONITOR_SAMPLE_INTERVAL_MS);
        set_state_machine_timeout(app, TIMEOUT_MONITOR_MS);
        s_last_sample_ms = app->last_now_ms;
        app->sub_step = 1;
        break;

    case 1:
    {
        /* 检查采样间隔 */
        if ((app->last_now_ms - s_last_sample_ms) < MONITOR_SAMPLE_INTERVAL_MS) {
            break; /* 未到采样时刻，等待 */
        }
        s_last_sample_ms = app->last_now_ms;

        /* 检查采样次数上限 */
        if (app->result.sample_count >= MONITOR_MAX_SAMPLES) {
            LOG_I("达到最大采样次数 %lu，结束监控",
                  (unsigned long)MONITOR_MAX_SAMPLES);
            set_state_machine_state(app, INA226_STATE_DUMP, "采样上限");
            return;
        }

        /* 读取完整测量结果 */
        ret = ina226_read_all(&meas);
        if (ret != INA226_OK) {
            LOG_E("读取测量结果失败 (ret=%d)", ret);
            enter_error_state(app, "MONITOR 读取失败");
            return;
        }

        app->result.sample_count++;

        /* 统计极值和平均值 */
        if (meas.bus_voltage_V > app->result.vbus_max_V) {
            app->result.vbus_max_V = meas.bus_voltage_V;
        }
        if (meas.bus_voltage_V < app->result.vbus_min_V) {
            app->result.vbus_min_V = meas.bus_voltage_V;
        }
        if (meas.current_A > app->result.current_max_A) {
            app->result.current_max_A = meas.current_A;
        }
        if (meas.current_A < app->result.current_min_A) {
            app->result.current_min_A = meas.current_A;
        }
        if (meas.power_W > app->result.power_max_W) {
            app->result.power_max_W = meas.power_W;
        }

        /* 移动平均: 累加 */
        app->result.vbus_avg_V    += meas.bus_voltage_V;
        app->result.current_avg_A += meas.current_A;
        app->result.power_avg_W   += meas.power_W;

        if (meas.math_overflow) {
            app->result.overflow_count++;
        }

        /* ---- 保护阈值检查 ---- */
        uint8_t fault_detected = 0;

        if (meas.bus_voltage_V > INA226_APP_VBUS_OV_THRESHOLD_V) {
            app->result.vbus_ov_triggered = 1;
            fault_detected = 1;
            LOG_E("过压告警: Vbus=%.3fV > %.3fV!",
                  meas.bus_voltage_V, INA226_APP_VBUS_OV_THRESHOLD_V);
        }

        if (meas.bus_voltage_V < INA226_APP_VBUS_UV_THRESHOLD_V) {
            app->result.vbus_uv_triggered = 1;
            fault_detected = 1;
            LOG_E("欠压告警: Vbus=%.3fV < %.3fV!",
                  meas.bus_voltage_V, INA226_APP_VBUS_UV_THRESHOLD_V);
        }

        if (meas.current_A > INA226_APP_CURRENT_OC_THRESHOLD_A) {
            app->result.current_oc_triggered = 1;
            fault_detected = 1;
            LOG_E("过流告警: I=%.3fA > %.3fA!",
                  meas.current_A, INA226_APP_CURRENT_OC_THRESHOLD_A);
        }

        if (fault_detected) {
            app->result.alert_count++;
            /* 去抖: 连续 N 次触发再跳转 */
            static uint8_t s_debounce = 0;
            s_debounce++;
            if (s_debounce >= INA226_APP_ALERT_DEBOUNCE_CNT) {
                s_debounce = 0;
                set_state_machine_state(app, INA226_STATE_CHECK_ALERT,
                                         "保护阈值触发");
                return;
            }
        } else {
            /* 重置去抖计数 */
        }

        /* 定期打印状态 (每 10 次采样打印一次) */
        if ((app->result.sample_count % 10) == 0) {
            LOG_I("[%5lu] Vbus=%.3fV | I=%.3fA | P=%.3fW | %s %s",
                  (unsigned long)app->result.sample_count,
                  meas.bus_voltage_V,
                  meas.current_A,
                  meas.power_W,
                  meas.math_overflow ? "[OVF!]" : "",
                  meas.conversion_ready ? "[RDY]"  : "");
        }
        break;
    }

    default:
        enter_error_state(app, "MONITOR sub_step 越界");
        break;
    }
}

/**
 * @brief CHECK_ALERT 状态: 处理保护触发
 *
 * 在真实 BMS 场景中，此处可执行:
 *   - 切断充电 FET / 放电 FET
 *   - 拉低 EN 引脚关断电源路径
 *   - 通过 Alert 引脚通知外部 MCU
 *   - 记录故障日志到 EEPROM
 *
 * 示例中仅打印告警详情并跳转 DUMP。
 */
static void process_state_check_alert(ina226_app_t *app)
{
    switch (app->sub_step) {
    case 0:
        LOG_I("处理保护告警 ...");

        if (app->result.vbus_ov_triggered) {
            LOG_E(">>> 过压保护: Vbus_max = %.3fV (阈值 %.3fV)",
                  app->result.vbus_max_V, INA226_APP_VBUS_OV_THRESHOLD_V);
        }
        if (app->result.vbus_uv_triggered) {
            LOG_E(">>> 欠压保护: Vbus_min = %.3fV (阈值 %.3fV)",
                  app->result.vbus_min_V, INA226_APP_VBUS_UV_THRESHOLD_V);
        }
        if (app->result.current_oc_triggered) {
            LOG_E(">>> 过流保护: I_max = %.3fA (阈值 %.3fA)",
                  app->result.current_max_A, INA226_APP_CURRENT_OC_THRESHOLD_A);
        }

        LOG_I("总报警次数: %lu | 溢出次数: %lu",
              (unsigned long)app->result.alert_count,
              (unsigned long)app->result.overflow_count);

        /* 在真实系统中此处应执行保护动作:
         *   - charge_enable(0);
         *   - discharge_enable(0);
         *   - 记录故障事件;
         */

        app->sub_step = 1;
        break;

    case 1:
        /* 转储寄存器便于故障分析 */
        set_state_machine_state(app, INA226_STATE_DUMP, "CHECK_ALERT 完成");
        break;

    default:
        enter_error_state(app, "CHECK_ALERT sub_step 越界");
        break;
    }
}

/**
 * @brief DUMP 状态: 寄存器转储 + 监控汇总
 */
static void process_state_dump(ina226_app_t *app)
{
    switch (app->sub_step) {
    case 0:
        set_state_machine_timeout(app, TIMEOUT_DUMP_MS);
        app->sub_step = 1;
        break;

    case 1:
        /* 转储全部寄存器 */
        ina226_dump_registers();
        app->sub_step = 2;
        break;

    case 2:
    {
        /* 计算平均值 */
        if (app->result.sample_count > 0) {
            app->result.vbus_avg_V    /= (float)app->result.sample_count;
            app->result.current_avg_A /= (float)app->result.sample_count;
            app->result.power_avg_W   /= (float)app->result.sample_count;
        }

        LOG_I("============== 监控汇总 ==============");
        LOG_I("  采样次数       : %lu",
              (unsigned long)app->result.sample_count);
        LOG_I("  Vbus (V)       : Max=%.3f  Min=%.3f  Avg=%.3f",
              app->result.vbus_max_V,
              app->result.vbus_min_V,
              app->result.vbus_avg_V);
        LOG_I("  Current (A)    : Max=%.3f  Min=%.3f  Avg=%.3f",
              app->result.current_max_A,
              app->result.current_min_A,
              app->result.current_avg_A);
        LOG_I("  Power (W)      : Max=%.3f  Avg=%.3f",
              app->result.power_max_W,
              app->result.power_avg_W);
        LOG_I("  Alert Count    : %lu",
              (unsigned long)app->result.alert_count);
        LOG_I("  Overflow Count : %lu",
              (unsigned long)app->result.overflow_count);
        LOG_I("  Protections    : OV=%s  UV=%s  OC=%s",
              app->result.vbus_ov_triggered  ? "YES" : "NO",
              app->result.vbus_uv_triggered  ? "YES" : "NO",
              app->result.current_oc_triggered ? "YES" : "NO");
        LOG_I("========================================");
        app->sub_step = 3;
        break;
    }

    case 3:
        /* 跳转到 IDLE，监控结束 */
        set_state_machine_state(app, INA226_STATE_IDLE, "DUMP 完成");
        break;

    default:
        enter_error_state(app, "DUMP sub_step 越界");
        break;
    }
}

/* ==================== 状态处理函数分发表 ==================== */

typedef void (*ina226_state_handler)(ina226_app_t *app);

static const ina226_state_handler s_state_handlers[] = {
    [INA226_STATE_INIT]        = process_state_init,
    [INA226_STATE_CHECK_ID]    = process_state_check_id,
    [INA226_STATE_CONFIGURE]   = process_state_configure,
    [INA226_STATE_MONITOR]     = process_state_monitor,
    [INA226_STATE_CHECK_ALERT] = process_state_check_alert,
    [INA226_STATE_DUMP]        = process_state_dump,
};

/* ==================== 公开 API ==================== */

void ina226_app_init(ina226_app_t *app)
{
    if (!app) return;

    memset(app, 0, sizeof(ina226_app_t));

    app->task_state    = INA226_STATE_INIT;
    app->last_state    = INA226_STATE_IDLE;
    app->timeout_state = INA226_STATE_TIMEOUT;

    s_app = app;

    LOG_I("INA226 应用实例初始化完成");
}

void ina226_app_process(ina226_app_t *app, uint32_t now_ms)
{
    if (!app) return;

    app->last_now_ms = now_ms;

    /* ---- 超时监控 ---- */
    if (app->timeout_ms > 0 &&
        (now_ms - app->timeout_start_ms) >= app->timeout_ms) {

        ina226_app_state_t prev_state = app->task_state;

        LOG_E("状态 <%s> 超时 (%lums / %lums)，跳转到 <%s>",
              s_state_names[prev_state],
              (unsigned long)(now_ms - app->timeout_start_ms),
              (unsigned long)app->timeout_ms,
              s_state_names[app->timeout_state]);

        app->last_state = prev_state;
        app->task_state = app->timeout_state;
        app->sub_step   = 0;
        app->timeout_ms = 0;
    }

    /* ---- 状态分发 ---- */
    ina226_app_state_t this_state = app->task_state;

    if (this_state == INA226_STATE_IDLE) {
        /* IDLE 状态不做任何操作 */
        return;
    }

    if (this_state == INA226_STATE_TIMEOUT) {
        LOG_E("TIMEOUT 状态: 上一状态 <%s> 已超时，应用停止",
              s_state_names[app->last_state]);
        app->task_state = INA226_STATE_ERROR;
        return;
    }

    if (this_state == INA226_STATE_ERROR) {
        /* ERROR 状态: 停止处理，等待外部复位 */
        return;
    }

    if (this_state >= INA226_STATE_COUNT ||
        s_state_handlers[this_state] == NULL) {
        LOG_E("未注册的状态处理器: %d", this_state);
        enter_error_state(app, "未注册的状态处理器");
        return;
    }

    s_state_handlers[this_state](app);
}
