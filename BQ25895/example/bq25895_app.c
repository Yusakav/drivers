/**
 * @file    bq25895_app.c
 * @brief   BQ25895 充电管理应用层示例
 * @details 实现完整的充电管理状态机，展示典型充电管理流程：
 *          - 检测适配器插入/移除
 *          - 配置充电参数
 *          - 监控充电状态
 *          - 充满/故障处理
 *          - 电池温度监控
 *          - OTG 模式切换
 *          - 低功耗/运输模式
 *
 *          状态机: IDLE -> DETECT -> PRECHARGE -> CC -> CV -> DONE -> (FAULT)
 *          每个阶段通过 sub_step 机制分步推进，每次 app_process() 调用推进一个子步骤。
 * @version 1.0.0
 * @date    2026-06-13
 */

/*===========================================================================
 * INCLUDES
 *===========================================================================*/
#include "bq25895_app.h"
#include "bq25895.h"
#include "bq25895_abstraction.h"
#include <stddef.h>
#include <string.h>

/*===========================================================================
 * CONSTANTS
 *===========================================================================*/
#define BQ25895_APP_WD_FEED_INTERVAL_MS           5000U   /**< 喂狗间隔: 5s (40s 定时器) */
#define BQ25895_APP_STATUS_SAMPLE_INTERVAL_MS     1000U   /**< 状态采样间隔: 1s */
#define BQ25895_APP_PRECHARGE_TIMEOUT_MS          3600000U /**< 预充电超时: 1 小时 */
#define BQ25895_APP_FAULT_RETRY_MAX               3U      /**< 故障重试最大次数 */
#define BQ25895_APP_ADC_RETRY_MAX                 5U      /**< ADC 读取重试次数 */
#define BQ25895_APP_RECHECK_DELAY_MS              1000U   /**< 重新检查延时: 1s */
#define BQ25895_APP_BAT_PRESENT_THRESHOLD_MV      2500U   /**< 电池存在判断电压: 2.5V */
#define BQ25895_APP_BAT_FULL_DELTA_MV             50U     /**< 充满判断电压裕量: 50mV */

/*===========================================================================
 * FUNCTION PROTOTYPES (State Machine Sub-Processors)
 *===========================================================================*/
static int32_t process_phase_idle(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms);
static int32_t process_phase_detect(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms);
static int32_t process_phase_precharge(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms);
static int32_t process_phase_cc(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms);
static int32_t process_phase_cv(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms);
static int32_t process_phase_done(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms);
static int32_t process_phase_fault(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms);
static int32_t process_phase_otg(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms);
static int32_t process_phase_ship(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms);

/*===========================================================================
 * GLOBAL INSTANCE
 *===========================================================================*/
bq25895_charge_ctx_t g_bq25895_ctx;

/*===========================================================================
 * HELPER: ADC Reading with Retry
 *===========================================================================*/
static int32_t read_adc_with_retry(bq25895_adc_result_t *adc)
{
    int32_t ret;
    for (uint8_t i = 0U; i < BQ25895_APP_ADC_RETRY_MAX; i++) {
        ret = bq25895_adc_start_conversion();
        if (ret != BQ25895_OK) continue;
        /* ADC 转换最大 1s，等待 50ms + 轮询检查 */
        ret = bq25895_get_adc_all(adc);
        if (ret == BQ25895_OK) return BQ25895_OK;
    }
    return ret;
}

/*===========================================================================
 * HELPER: Phase Name Strings
 *===========================================================================*/
const char* bq25895_app_phase_name(bq25895_charge_phase_t phase)
{
    switch (phase) {
        case BQ25895_CHARGE_PHASE_IDLE:       return "IDLE";
        case BQ25895_CHARGE_PHASE_DETECT:     return "DETECT";
        case BQ25895_CHARGE_PHASE_PRECHARGE:  return "PRECHARGE";
        case BQ25895_CHARGE_PHASE_CC:         return "CC";
        case BQ25895_CHARGE_PHASE_CV:         return "CV";
        case BQ25895_CHARGE_PHASE_DONE:       return "DONE";
        case BQ25895_CHARGE_PHASE_FAULT:      return "FAULT";
        case BQ25895_CHARGE_PHASE_OTG:        return "OTG";
        case BQ25895_CHARGE_PHASE_SHIP:       return "SHIP";
        default:                               return "UNKNOWN";
    }
}

const char* bq25895_app_vbus_name(bq25895_vbus_stat_t vbus_stat)
{
    switch (vbus_stat) {
        case BQ25895_VBUS_UNKNOWN:          return "No Input";
        case BQ25895_VBUS_USB_SDP:          return "USB SDP (500mA)";
        case BQ25895_VBUS_USB_CDP:          return "USB CDP (1.5A)";
        case BQ25895_VBUS_USB_DCP:          return "USB DCP (3.25A)";
        case BQ25895_VBUS_MAX_CHARGE:       return "MaxCharge (Adj HV DCP)";
        case BQ25895_VBUS_UNKNOWN_ADAPTER:  return "Unknown Adapter (500mA)";
        case BQ25895_VBUS_NON_STD_ADAPTER:  return "Non-Std Adapter";
        case BQ25895_VBUS_OTG:              return "OTG";
        default:                             return "Invalid";
    }
}

/*===========================================================================
 * PUBLIC: Initialize Charge Context
 *===========================================================================*/
int32_t bq25895_app_init(bq25895_charge_ctx_t *ctx,
                          const bq25895_battery_config_t *bat_cfg)
{
    if (ctx == NULL || bat_cfg == NULL) {
        return BQ25895_ERR_PARAM;
    }

    memset(ctx, 0, sizeof(bq25895_charge_ctx_t));

    /* 复制电池配置 */
    ctx->bat_cfg = *bat_cfg;

    /* 初始化状态 */
    ctx->phase              = BQ25895_CHARGE_PHASE_IDLE;
    ctx->prev_phase         = BQ25895_CHARGE_PHASE_IDLE;
    ctx->adapter_present    = false;
    ctx->charge_complete    = false;
    ctx->otg_requested      = false;
    ctx->fault_retry_cnt    = 0U;
    ctx->sample_interval_ms = BQ25895_APP_STATUS_SAMPLE_INTERVAL_MS;

    /* 配置 BQ25895 硬件参数 */
    int32_t ret;

    /* 输入限流 */
    ret = bq25895_set_input_current_limit(bat_cfg->input_current_ma);
    if (ret != BQ25895_OK) return ret;

    /* 充电参数 */
    ret = bq25895_configure_charge_params(bat_cfg->charge_voltage_mv,
                                           bat_cfg->charge_current_ma,
                                           bat_cfg->precharge_current_ma,
                                           bat_cfg->termination_current_ma);
    if (ret != BQ25895_OK) return ret;

    /* 系统最低电压 */
    ret = bq25895_set_sys_min_voltage(bat_cfg->sys_min_mv);
    if (ret != BQ25895_OK) return ret;

    /* 看门狗 */
    ret = bq25895_set_watchdog(bat_cfg->watchdog);
    if (ret != BQ25895_OK) return ret;

    /* 充电终止 */
    ret = bq25895_set_termination_enable(bat_cfg->enable_term);
    if (ret != BQ25895_OK) return ret;

    /* 安全定时器 */
    ret = bq25895_set_safety_timer_enable(bat_cfg->enable_safety_timer);
    if (ret != BQ25895_OK) return ret;

    ret = bq25895_set_charge_timer(bat_cfg->chg_timer);
    if (ret != BQ25895_OK) return ret;

    /* 默认禁能充电，在 DETECT 阶段再启用 */
    ret = bq25895_set_charge_enable(false);
    if (ret != BQ25895_OK) return ret;

    return BQ25895_OK;
}

/*===========================================================================
 * PUBLIC: Main Process
 *===========================================================================*/
int32_t bq25895_app_process(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms)
{
    if (ctx == NULL) {
        return BQ25895_ERR_PARAM;
    }

    int32_t ret = BQ25895_OK;

    /* 喂狗 (如使能) */
    if (ctx->bat_cfg.watchdog != BQ25895_WDT_DISABLE) {
        if (current_time_ms - ctx->last_wd_feed_ms >= BQ25895_APP_WD_FEED_INTERVAL_MS) {
            bq25895_watchdog_reset();
            ctx->last_wd_feed_ms = current_time_ms;
        }
    }

    /* 按阶段分派处理 */
    switch (ctx->phase) {
        case BQ25895_CHARGE_PHASE_IDLE:
            ret = process_phase_idle(ctx, current_time_ms);
            break;
        case BQ25895_CHARGE_PHASE_DETECT:
            ret = process_phase_detect(ctx, current_time_ms);
            break;
        case BQ25895_CHARGE_PHASE_PRECHARGE:
            ret = process_phase_precharge(ctx, current_time_ms);
            break;
        case BQ25895_CHARGE_PHASE_CC:
            ret = process_phase_cc(ctx, current_time_ms);
            break;
        case BQ25895_CHARGE_PHASE_CV:
            ret = process_phase_cv(ctx, current_time_ms);
            break;
        case BQ25895_CHARGE_PHASE_DONE:
            ret = process_phase_done(ctx, current_time_ms);
            break;
        case BQ25895_CHARGE_PHASE_FAULT:
            ret = process_phase_fault(ctx, current_time_ms);
            break;
        case BQ25895_CHARGE_PHASE_OTG:
            ret = process_phase_otg(ctx, current_time_ms);
            break;
        case BQ25895_CHARGE_PHASE_SHIP:
            ret = process_phase_ship(ctx, current_time_ms);
            break;
        default:
            ctx->phase = BQ25895_CHARGE_PHASE_IDLE;
            break;
    }

    return ret;
}

/*===========================================================================
 * STATE MACHINE: IDLE
 * 等待适配器插入，检测 VBUS 电压
 *===========================================================================*/
static int32_t process_phase_idle(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms)
{
    static uint8_t sub_step = 0U;
    int32_t ret;

    sub_step++;
    switch (sub_step) {
        case 1:
            /* 检查 OTG 请求 */
            if (ctx->otg_requested) {
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_OTG;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 2:
            /* 读取 VBUS 状态 */
            ret = bq25895_get_vbus_status(&ctx->status.vbus_stat);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }

            if (ctx->status.vbus_stat != BQ25895_VBUS_UNKNOWN &&
                ctx->status.vbus_stat != BQ25895_VBUS_OTG) {
                /* 适配器已插入 */
                ctx->adapter_present = true;
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_DETECT;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        default:
            /* 无适配器，继续等待 */
            sub_step = 0U;
            return BQ25895_OK;
    }
}

/*===========================================================================
 * STATE MACHINE: DETECT
 * 检测适配器类型、电池状态，配置充电参数并启动充电
 *===========================================================================*/
static int32_t process_phase_detect(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms)
{
    static uint8_t sub_step = 0U;
    int32_t ret;

    sub_step++;
    switch (sub_step) {
        case 1:
            /* 读取状态 */
            ret = bq25895_get_full_status(&ctx->status);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_FAULT;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 2:
            /* 检查适配器是否仍然存在 */
            if (ctx->status.vbus_stat == BQ25895_VBUS_UNKNOWN ||
                ctx->status.vbus_stat == BQ25895_VBUS_OTG) {
                /* 适配器已移除 */
                ctx->adapter_present = false;
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_IDLE;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 3:
            /* 读取电池电压 ADC */
            ret = bq25895_get_battery_voltage(&ctx->adc.vbat_mv);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }

            /* 检查电池是否连接 */
            if (ctx->adc.vbat_mv < BQ25895_APP_BAT_PRESENT_THRESHOLD_MV) {
                /* 电池未连接 */
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_IDLE;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 4:
            /* 根据适配器类型调整输入限流 */
            switch (ctx->status.vbus_stat) {
                case BQ25895_VBUS_USB_SDP:
                    bq25895_set_input_current_limit(500U);
                    break;
                case BQ25895_VBUS_USB_CDP:
                    bq25895_set_input_current_limit(1500U);
                    break;
                case BQ25895_VBUS_USB_DCP:
                    bq25895_set_input_current_limit(ctx->bat_cfg.input_current_ma);
                    break;
                default:
                    bq25895_set_input_current_limit(500U);
                    break;
            }
            /* fall through */

        case 5:
            /* 使能充电 */
            ret = bq25895_set_charge_enable(true);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            /* fall through */

        case 6:
            /* 判断进入哪个充电阶段 */
            if (ctx->adc.vbat_mv < 2800U) {
                /* 电池电压低于 2.8V: 预充电 */
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_PRECHARGE;
                ctx->phase_enter_ms = current_time_ms;
            } else if (ctx->adc.vbat_mv < ctx->bat_cfg.charge_voltage_mv - BQ25895_APP_BAT_FULL_DELTA_MV) {
                /* 电池未满: 快充 */
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_CC;
                ctx->phase_enter_ms = current_time_ms;
            } else {
                /* 电池接近满充: 进入 CV 或 DONE */
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_CV;
                ctx->phase_enter_ms = current_time_ms;
            }
            return BQ25895_OK;

        default:
            sub_step = 0U;
            return BQ25895_OK;
    }
}

/*===========================================================================
 * STATE MACHINE: PRECHARGE
 * 小电流预充电，监控电池电压升高
 *===========================================================================*/
static int32_t process_phase_precharge(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms)
{
    static uint8_t sub_step = 0U;
    int32_t ret;

    sub_step++;
    switch (sub_step) {
        case 1:
            /* 检查预充电超时 */
            if (current_time_ms - ctx->phase_enter_ms > BQ25895_APP_PRECHARGE_TIMEOUT_MS) {
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_FAULT;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 2:
            /* 检查适配器 */
            ret = bq25895_get_vbus_status(&ctx->status.vbus_stat);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            if (ctx->status.vbus_stat == BQ25895_VBUS_UNKNOWN ||
                ctx->status.vbus_stat == BQ25895_VBUS_OTG) {
                ctx->adapter_present = false;
                bq25895_set_charge_enable(false);
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_IDLE;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 3:
            /* 读取电池电压 */
            ret = bq25895_get_battery_voltage(&ctx->adc.vbat_mv);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }

            /* 检查是否离开预充电 */
            if (ctx->adc.vbat_mv >= 3000U) {
                /* 电池电压 >= 3.0V: 进入快充 */
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_CC;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }

            /* 检查故障 */
            ret = bq25895_get_fault(&ctx->fault);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            if (ctx->fault.chrg_fault != BQ25895_CHRG_FAULT_NORMAL ||
                ctx->fault.bat_fault ||
                ctx->fault.ntc_fault != BQ25895_NTC_NORMAL) {
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_FAULT;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }

            sub_step = 0U;
            return BQ25895_OK;

        default:
            sub_step = 0U;
            return BQ25895_OK;
    }
}

/*===========================================================================
 * STATE MACHINE: CC (Constant Current)
 * 恒流快速充电
 *===========================================================================*/
static int32_t process_phase_cc(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms)
{
    static uint8_t sub_step = 0U;
    int32_t ret;

    sub_step++;
    switch (sub_step) {
        case 1:
            /* 读取充电状态 */
            ret = bq25895_get_charge_status(&ctx->status.chrg_stat);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }

            /* 检查是否已进入 CV / 完成 */
            if (ctx->status.chrg_stat == BQ25895_CHRG_TERM_DONE) {
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_DONE;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 2:
            /* 读取电池电压 */
            ret = bq25895_get_battery_voltage(&ctx->adc.vbat_mv);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }

            /* 电池电压接近目标 -> 进入 CV */
            if (ctx->adc.vbat_mv >= ctx->bat_cfg.charge_voltage_mv - BQ25895_APP_BAT_FULL_DELTA_MV) {
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_CV;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 3:
            /* 检查适配器 */
            ret = bq25895_get_vbus_status(&ctx->status.vbus_stat);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            if (ctx->status.vbus_stat == BQ25895_VBUS_UNKNOWN ||
                ctx->status.vbus_stat == BQ25895_VBUS_OTG) {
                ctx->adapter_present = false;
                bq25895_set_charge_enable(false);
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_IDLE;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 4:
            /* 检查故障 */
            ret = bq25895_get_fault(&ctx->fault);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }

            if (ctx->fault.chrg_fault != BQ25895_CHRG_FAULT_NORMAL ||
                ctx->fault.bat_fault ||
                ctx->fault.ntc_fault != BQ25895_NTC_NORMAL) {
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_FAULT;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        default:
            /* 仍在 CC 阶段，继续 */
            sub_step = 0U;
            return BQ25895_OK;
    }
}

/*===========================================================================
 * STATE MACHINE: CV (Constant Voltage)
 * 恒压充电阶段
 *===========================================================================*/
static int32_t process_phase_cv(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms)
{
    static uint8_t sub_step = 0U;
    int32_t ret;

    sub_step++;
    switch (sub_step) {
        case 1:
            /* 读取充电状态 */
            ret = bq25895_get_charge_status(&ctx->status.chrg_stat);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }

            /* 充电完成 */
            if (ctx->status.chrg_stat == BQ25895_CHRG_TERM_DONE) {
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_DONE;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 2:
            /* 读取实际充电电流 */
            ret = bq25895_get_charge_current_adc(&ctx->adc.ichg_ma);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }

            /* 电流降到终止电流以下 -> 接近完成 */
            if (ctx->adc.ichg_ma <= ctx->bat_cfg.termination_current_ma) {
                /* BQ25895 硬件会自动终止，但也可以手动检查 */
            }
            /* fall through */

        case 3:
            /* 检查适配器 */
            ret = bq25895_get_vbus_status(&ctx->status.vbus_stat);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            if (ctx->status.vbus_stat == BQ25895_VBUS_UNKNOWN ||
                ctx->status.vbus_stat == BQ25895_VBUS_OTG) {
                ctx->adapter_present = false;
                bq25895_set_charge_enable(false);
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_IDLE;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 4:
            /* 检查故障 */
            ret = bq25895_get_fault(&ctx->fault);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            if (ctx->fault.chrg_fault != BQ25895_CHRG_FAULT_NORMAL ||
                ctx->fault.bat_fault ||
                ctx->fault.ntc_fault != BQ25895_NTC_NORMAL) {
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_FAULT;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        default:
            sub_step = 0U;
            return BQ25895_OK;
    }
}

/*===========================================================================
 * STATE MACHINE: DONE
 * 充电完成，等待适配器移除或再充电条件
 *===========================================================================*/
static int32_t process_phase_done(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms)
{
    static uint8_t sub_step = 0U;
    int32_t ret;

    sub_step++;
    switch (sub_step) {
        case 1:
            ctx->charge_complete = true;
            /* fall through */

        case 2:
            /* 检查适配器是否移除 */
            ret = bq25895_get_vbus_status(&ctx->status.vbus_stat);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            if (ctx->status.vbus_stat == BQ25895_VBUS_UNKNOWN ||
                ctx->status.vbus_stat == BQ25895_VBUS_OTG) {
                ctx->adapter_present = false;
                ctx->charge_complete = false;
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_IDLE;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        case 3:
            /* 读取电池电压检查是否需要再充电 */
            ret = bq25895_get_battery_voltage(&ctx->adc.vbat_mv);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }

            /* 再充电条件: 电池电压低于 VREG - VRECHG (100mV/200mV) */
            if (ctx->adc.vbat_mv < ctx->bat_cfg.charge_voltage_mv - 300U) {
                ctx->charge_complete = false;
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_DETECT;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }
            /* fall through */

        default:
            sub_step = 0U;
            return BQ25895_OK;
    }
}

/*===========================================================================
 * STATE MACHINE: FAULT
 * 故障处理：读取故障类型，尝试恢复
 *===========================================================================*/
static int32_t process_phase_fault(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms)
{
    static uint8_t sub_step = 0U;
    int32_t ret;

    sub_step++;
    switch (sub_step) {
        case 1:
            /* 禁能充电 */
            bq25895_set_charge_enable(false);
            /* fall through */

        case 2:
            /* 读取并清除故障 */
            ret = bq25895_get_fault(&ctx->fault);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            /* fall through */

        case 3:
            /* 检查故障是否已恢复 */
            if (ctx->fault.chrg_fault == BQ25895_CHRG_FAULT_NORMAL &&
                !ctx->fault.bat_fault &&
                ctx->fault.ntc_fault == BQ25895_NTC_NORMAL &&
                !ctx->fault.boost_fault) {

                /* 故障已清除 */
                ctx->fault_retry_cnt = 0U;
                sub_step = 0U;

                /* 检查适配器 */
                bq25895_get_vbus_status(&ctx->status.vbus_stat);
                if (ctx->status.vbus_stat != BQ25895_VBUS_UNKNOWN &&
                    ctx->status.vbus_stat != BQ25895_VBUS_OTG) {
                    ctx->prev_phase = ctx->phase;
                    ctx->phase = BQ25895_CHARGE_PHASE_DETECT;
                    ctx->phase_enter_ms = current_time_ms;
                } else {
                    ctx->prev_phase = ctx->phase;
                    ctx->phase = BQ25895_CHARGE_PHASE_IDLE;
                    ctx->phase_enter_ms = current_time_ms;
                }
                return BQ25895_OK;
            }

            /* 故障仍然存在 */
            ctx->fault_retry_cnt++;
            if (ctx->fault_retry_cnt >= BQ25895_APP_FAULT_RETRY_MAX) {
                /* 超过重试次数，保持在 FAULT 状态 */
                sub_step = 0U;
                return BQ25895_ERR_FAULT;
            }

            /* 等待后重试 */
            sub_step = 0U;
            return BQ25895_OK;

        default:
            sub_step = 0U;
            return BQ25895_OK;
    }
}

/*===========================================================================
 * STATE MACHINE: OTG
 * OTG 升压模式处理
 *===========================================================================*/
static int32_t process_phase_otg(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms)
{
    static uint8_t sub_step = 0U;
    int32_t ret;

    sub_step++;
    switch (sub_step) {
        case 1:
            /* 禁能充电 */
            ret = bq25895_set_charge_enable(false);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            /* fall through */

        case 2:
            /* 配置 OTG 电压 */
            {
                uint16_t otg_v = BQ25895_APP_OTG_VOLTAGE_5V1;
                switch (ctx->otg_voltage) {
                    case BQ25895_OTG_VOLTAGE_5V:  otg_v = BQ25895_APP_OTG_VOLTAGE_5V0; break;
                    case BQ25895_OTG_VOLTAGE_5V1: otg_v = BQ25895_APP_OTG_VOLTAGE_5V1; break;
                    case BQ25895_OTG_VOLTAGE_5V5: otg_v = BQ25895_APP_OTG_VOLTAGE_5V5; break;
                    default: break;
                }
                ret = bq25895_set_boost_voltage(otg_v);
            }
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            /* fall through */

        case 3:
            /* 使能 OTG */
            ret = bq25895_set_otg_config(true);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            /* fall through */

        case 4:
            /* 监控 OTG 状态 */
            ret = bq25895_get_fault(&ctx->fault);
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }

            /* OTG 故障检查 */
            if (ctx->fault.boost_fault) {
                bq25895_clear_faults();
                ctx->fault_retry_cnt++;
                if (ctx->fault_retry_cnt >= BQ25895_APP_FAULT_RETRY_MAX) {
                    bq25895_set_otg_config(false);
                    ctx->otg_requested = false;
                    sub_step = 0U;
                    ctx->prev_phase = ctx->phase;
                    ctx->phase = BQ25895_CHARGE_PHASE_FAULT;
                    ctx->phase_enter_ms = current_time_ms;
                    return BQ25895_OK;
                }
            } else {
                ctx->fault_retry_cnt = 0U;
            }

            /* 用户请求退出 OTG */
            if (!ctx->otg_requested) {
                bq25895_set_otg_config(false);
                sub_step = 0U;
                ctx->prev_phase = ctx->phase;
                ctx->phase = BQ25895_CHARGE_PHASE_IDLE;
                ctx->phase_enter_ms = current_time_ms;
                return BQ25895_OK;
            }

            sub_step = 0U;
            return BQ25895_OK;

        default:
            sub_step = 0U;
            return BQ25895_OK;
    }
}

/*===========================================================================
 * STATE MACHINE: SHIP (Ship Mode)
 * 运输模式: 关断 BATFET，最小功耗
 *===========================================================================*/
static int32_t process_phase_ship(bq25895_charge_ctx_t *ctx, uint32_t current_time_ms)
{
    static uint8_t sub_step = 0U;
    int32_t ret;

    sub_step++;
    switch (sub_step) {
        case 1:
            /* 进入运输模式 */
            ret = bq25895_enter_ship_mode();
            if (ret != BQ25895_OK) {
                sub_step = 0U;
                return ret;
            }
            /* BQ25895 进入运输模式后 BATFET 关断，系统断电。
               此后只能通过适配器插入或 QON 引脚唤醒。
               状态机保持 SHIP 直到外部唤醒。 */
            sub_step = 0U;
            return BQ25895_OK;

        default:
            sub_step = 0U;
            return BQ25895_OK;
    }
}

/*===========================================================================
 * PUBLIC: OTG Request / Exit
 *===========================================================================*/
int32_t bq25895_app_request_otg(bq25895_charge_ctx_t *ctx,
                                 bq25895_otg_voltage_t voltage)
{
    if (ctx == NULL) return BQ25895_ERR_PARAM;

    ctx->otg_requested = true;
    ctx->otg_voltage   = voltage;
    ctx->fault_retry_cnt = 0U;

    return BQ25895_OK;
}

int32_t bq25895_app_exit_otg(bq25895_charge_ctx_t *ctx)
{
    if (ctx == NULL) return BQ25895_ERR_PARAM;

    ctx->otg_requested = false;
    return BQ25895_OK;
}

int32_t bq25895_app_request_ship_mode(bq25895_charge_ctx_t *ctx)
{
    if (ctx == NULL) return BQ25895_ERR_PARAM;

    ctx->prev_phase = ctx->phase;
    ctx->phase       = BQ25895_CHARGE_PHASE_SHIP;

    return BQ25895_OK;
}

/*===========================================================================
 * DEMO: main() Example
 * 使用示例，展示如何初始化驱动并运行充电状态机主循环。
 *===========================================================================*/
#if defined(BQ25895_APP_DEMO_ENABLE)

#include <stdio.h>
#include <time.h>

/* ---------- 平台相关 I2C 实现示例 ---------- */
static int32_t platform_i2c_write(uint8_t dev_addr, uint8_t reg_addr, uint8_t data)
{
    /* TODO: 实现 MCU 平台的 I2C 写入 */
    (void)dev_addr; (void)reg_addr; (void)data;
    return 0;
}

static int32_t platform_i2c_read(uint8_t dev_addr, uint8_t reg_addr, uint8_t *data)
{
    /* TODO: 实现 MCU 平台的 I2C 读取 */
    (void)dev_addr; (void)reg_addr;
    if (data != NULL) *data = 0x00U;
    return 0;
}

static void platform_delay_us(uint32_t us)
{
    /* TODO: 实现 MCU 平台的微秒延时 */
    (void)us;
}

static uint32_t platform_get_time_ms(void)
{
    /* TODO: 实现 MCU 平台的毫秒时间戳 */
    return 0U;
}

/* ---------- 主函数示例 ---------- */
int main(void)
{
    int32_t ret;

    /* 1. 注册硬件抽象层 */
    bq25895_hal_ops_t hal_ops = {
        .i2c_write = platform_i2c_write,
        .i2c_read  = platform_i2c_read,
        .delay_us  = platform_delay_us,
    };

    /* 2. 初始化驱动 */
    ret = bq25895_init(&hal_ops);
    if (ret != BQ25895_OK) {
        printf("BQ25895 init failed: %d\n", ret);
        return -1;
    }
    printf("BQ25895 initialized successfully.\n");

    /* 3. 读取器件信息 */
    bq25895_device_info_t dev_info;
    ret = bq25895_get_device_info(&dev_info);
    if (ret == BQ25895_OK) {
        printf("Device: PN=%d, Rev=%d\n", dev_info.part_number, dev_info.dev_revision);
    }

    /* 4. 配置电池参数 */
    bq25895_battery_config_t bat_cfg = {
        .charge_voltage_mv       = BQ25895_APP_BAT_VOLTAGE_NORMAL_MV,  /* 4.208V */
        .charge_current_ma       = BQ25895_APP_CHG_CURRENT_2000MA,     /* 2A */
        .precharge_current_ma    = BQ25895_APP_PRECHARGE_CUR_128MA,    /* 128mA */
        .termination_current_ma  = BQ25895_APP_TERM_CUR_256MA,         /* 256mA */
        .input_current_ma        = BQ25895_APP_INPUT_CUR_2000MA,       /* 2A 输入限流 */
        .sys_min_mv              = 3500U,                               /* 3.5V */
        .enable_term             = true,
        .enable_safety_timer     = true,
        .chg_timer               = BQ25895_CHG_TIMER_12HRS,
        .watchdog                = BQ25895_WDT_40S,
    };

    /* 5. 初始化充电管理上下文 */
    ret = bq25895_app_init(&g_bq25895_ctx, &bat_cfg);
    if (ret != BQ25895_OK) {
        printf("App init failed: %d\n", ret);
        return -1;
    }
    printf("Charge manager initialized.\n");

    /* 6. 主循环 */
    while (1) {
        uint32_t now_ms = platform_get_time_ms();

        /* 推进充电状态机 */
        ret = bq25895_app_process(&g_bq25895_ctx, now_ms);

        /* 打印状态变化 */
        if (g_bq25895_ctx.prev_phase != g_bq25895_ctx.phase) {
            printf("[%s] -> [%s]\n",
                   bq25895_app_phase_name(g_bq25895_ctx.prev_phase),
                   bq25895_app_phase_name(g_bq25895_ctx.phase));

            if (g_bq25895_ctx.phase == BQ25895_CHARGE_PHASE_DETECT) {
                printf("  Adapter: %s\n",
                       bq25895_app_vbus_name(g_bq25895_ctx.status.vbus_stat));
            }

            if (g_bq25895_ctx.phase == BQ25895_CHARGE_PHASE_CC ||
                g_bq25895_ctx.phase == BQ25895_CHARGE_PHASE_CV) {
                printf("  VBAT=%umV, ICHG=%umA\n",
                       g_bq25895_ctx.adc.vbat_mv,
                       g_bq25895_ctx.adc.ichg_ma);
            }

            if (g_bq25895_ctx.phase == BQ25895_CHARGE_PHASE_DONE) {
                printf("  Charge complete! VBAT=%umV\n",
                       g_bq25895_ctx.adc.vbat_mv);
            }

            if (g_bq25895_ctx.phase == BQ25895_CHARGE_PHASE_FAULT) {
                printf("  Fault: CHRG=%d BAT=%d NTC=%d\n",
                       g_bq25895_ctx.fault.chrg_fault,
                       g_bq25895_ctx.fault.bat_fault,
                       g_bq25895_ctx.fault.ntc_fault);
            }
        }

        /* 周期性打印核心参数 (每 10s) */
        static uint32_t last_print_ms = 0U;
        if (now_ms - last_print_ms > 10000U) {
            last_print_ms = now_ms;

            bq25895_get_battery_voltage(&g_bq25895_ctx.adc.vbat_mv);
            bq25895_get_charge_current_adc(&g_bq25895_ctx.adc.ichg_ma);
            bq25895_get_vbus_status(&g_bq25895_ctx.status.vbus_stat);

            printf("[MON] Phase=%s VBAT=%umV ICHG=%umA VBUS=%s\n",
                   bq25895_app_phase_name(g_bq25895_ctx.phase),
                   g_bq25895_ctx.adc.vbat_mv,
                   g_bq25895_ctx.adc.ichg_ma,
                   bq25895_app_vbus_name(g_bq25895_ctx.status.vbus_stat));
        }

        /* 模拟延时 -- 实际平台应使用 RTOS 延时或定时器 */
        /* platform_delay_ms(100); */
    }

    return 0;
}

#endif /* BQ25895_APP_DEMO_ENABLE */
