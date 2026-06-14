/**
 * @file bq25710_example_input_power.h
 * @brief BQ25710 输入功率管理 - IDPM/VDPM/ILIM_HIZ + 峰值功率模式 公共接口
 *
 * 覆盖:
 *   - IDPM 动态输入电流调节
 *   - VDPM 输入电压跌落保护
 *   - ILIM_HIZ 硬件限流引脚
 *   - 两级峰值功率模式 (PEAK_POWER_MODE Level1/Level2)
 *   - ICO 输入电流优化器
 *
 * @note 基于已修复的 bq25710.h 驱动API
 */

#ifndef BQ25710_EXAMPLE_INPUT_POWER_H
#define BQ25710_EXAMPLE_INPUT_POWER_H

#include <stdint.h>
#include "bq25710.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * 常用适配器功率规格宏
 * ======================================================================== */

/** 45W USB-C 适配器: 19V/2.37A */
#define ADAPTER_45W_VOLTAGE_MV    19000
#define ADAPTER_45W_CURRENT_MA    2370

/** 65W USB-C 适配器: 20V/3.25A (典型笔记本适配器) */
#define ADAPTER_65W_VOLTAGE_MV    20000
#define ADAPTER_65W_CURRENT_MA    3250

/** 90W USB-C 适配器: 20V/4.5A */
#define ADAPTER_90W_VOLTAGE_MV    20000
#define ADAPTER_90W_CURRENT_MA    4500

/** 100W USB-C PD 适配器: 20V/5A */
#define ADAPTER_100W_VOLTAGE_MV   20000
#define ADAPTER_100W_CURRENT_MA   5000

/* ========================================================================
 * VDPM 常用阈值 (针对不同 USB 电源)
 * ======================================================================== */

/** USB 5V 适配器 VDPM 阈值: 4.2V */
#define VDPM_USB_5V_MV            4200

/** USB 9V 适配器 VDPM 阈值: 7.5V */
#define VDPM_USB_9V_MV            7500

/** USB 15V 适配器 VDPM 阈值: 13V */
#define VDPM_USB_15V_MV           13000

/** USB 20V 适配器 VDPM 阈值: 18V */
#define VDPM_USB_20V_MV           18000

/* ========================================================================
 * 峰值功率模式配置枚举
 * ======================================================================== */

/** 峰值功率过载触发时间 */
typedef enum {
    PKPWR_TOVLD_1MS  = 0,  /**< 1ms  (PKPWR_TOVLD_DEG=00) */
    PKPWR_TOVLD_2MS  = 1,  /**< 2ms  (PKPWR_TOVLD_DEG=01, 默认) */
    PKPWR_TOVLD_10MS = 2,  /**< 10ms (PKPWR_TOVLD_DEG=10) */
    PKPWR_TOVLD_20MS = 3   /**< 20ms (PKPWR_TOVLD_DEG=11) */
} pkpwr_tovld_t;

/** 峰值功率弛豫时间 (过载后恢复间隔) */
typedef enum {
    PKPWR_TMAX_5MS  = 0,  /**< 5ms  (PKPWR_TMAX=00) */
    PKPWR_TMAX_10MS = 1,  /**< 10ms (PKPWR_TMAX=01) */
    PKPWR_TMAX_20MS = 2,  /**< 20ms (PKPWR_TMAX=10, 默认) */
    PKPWR_TMAX_40MS = 3   /**< 40ms (PKPWR_TMAX=11) */
} pkpwr_tmax_t;

/* ========================================================================
 * 输入电源管理状态结构体
 * ======================================================================== */

typedef struct {
    uint16_t adapter_voltage_mv;    /**< 适配器当前电压 (mV) */
    uint16_t adapter_current_ma;    /**< 适配器当前电流 (mA) */
    uint16_t input_current_limit_ma; /**< 当前输入电流限制 (mA) */
    uint16_t vindpm_threshold_mv;   /**< 当前 VINDPM 阈值 (mV) */
    uint16_t dpm_actual_ma;         /**< DPM 环路实际生效电流 (mA) */
    uint8_t  in_iindpm      : 1;    /**< 处于 IINDPM 调节状态 */
    uint8_t  in_vindpm      : 1;    /**< 处于 VINDPM 调节状态 */
    uint8_t  pkpwr_overload : 1;    /**< 处于峰值功率过载周期 */
    uint8_t  pkpwr_relax    : 1;    /**< 处于峰值功率弛豫周期 */
} input_power_status_t;

/* ========================================================================
 * 公共 API 声明
 * ======================================================================== */

/**
 * @brief 初始化输入功率管理 (IDPM + VDPM + ILIM_HIZ)
 *
 * 配置流程:
 *   1. 设置 IIN_HOST 输入电流限制 (适配器额定值)
 *   2. 设置 VINDPM 输入电压跌落保护阈值
 *   3. 使能 IDPM 调节环路 (ChargeOption0 bit[1])
 *   4. 使能外部 ILIM_HIZ 引脚 (ChargeOption2 bit[7])
 *
 * @param input_current_ma   适配器额定输入电流 (mA)
 * @param vindpm_mv          输入电压最低阈值 (mV)
 * @return int8_t BQ25710_OK 或错误码
 */
int8_t input_power_init(uint16_t input_current_ma, uint16_t vindpm_mv);

/**
 * @brief 动态调整输入电流限制 (IDPM 调节)
 *
 * 当适配器过载时, 逐步降低输入电流以匹配适配器能力。
 * 适用于: 适配器热保护降额, USB PD 功率协商降级。
 *
 * @param new_current_ma 新的输入电流限制 (mA)
 * @return int8_t BQ25710_OK 或错误码
 */
int8_t input_power_adjust_idpm(uint16_t new_current_ma);

/**
 * @brief 读取输入电源管理状态
 *
 * 融合 ChargerStatus + IIN_DPM 寄存器 + 适配器检测的综合状态。
 *
 * @param status 输出参数, 状态结构体指针
 * @return int8_t BQ25710_OK 或错误码
 */
int8_t input_power_get_status(input_power_status_t *status);

/**
 * @brief 配置峰值功率模式 Level1 - IIN_HOST 阈值调节
 *
 * Level1 策略:
 *   - 适配器短时过载 (110%~230% 额定功率) 时仍由适配器+电池联合供电
 *   - CPU Turbo 模式下充分利用适配器短时过载能力
 *   - 过载后进入弛豫周期, 避免热累积
 *
 * 配置 ILIM2 = 150% ILIM1 (典型值):
 *   ProchotOption0[15:11] = 01001 (150%)
 *
 * @param overload_ms  过载持续时间 (1/2/10/20ms)
 * @param relax_ms     弛豫间隔 (5/10/20/40ms)
 * @param ilim2_pct    ILIM2 占 IIN_HOST 百分比 (110-230)
 * @return int8_t
 */
int8_t input_power_pkpwr_level1(uint8_t overload_ms, uint8_t relax_ms,
                                uint8_t ilim2_pct);

/**
 * @brief 配置峰值功率模式 Level2 - 仅电池补偿模式
 *
 * Level2 策略:
 *   - 当适配器功率严重不足时 (如 45W 适配器接 65W TDP CPU)
 *   - 适配器尽可能供给系统, 电池通过理想二极管补充缺口
 *   - 结合 VSYS 下冲触发峰值模式 (EN_PKPWR_VSYS=1)
 *
 * @return int8_t BQ25710_OK 或错误码
 */
int8_t input_power_pkpwr_level2(void);

/**
 * @brief 执行 ICO (输入电流优化器) 检测
 *
 * ICO 自动探测适配器最大可用电流:
 *   1. 使能 ICO 模式 (ChargeOption3 bit[11]=1)
 *   2. 逐步降低输入电流直至 VINDPM 不再触发
 *   3. 读取 IIN_DPM 寄存器获取实测最大电流
 *   4. 将该值写入 IIN_HOST 作为新的输入电流限制
 *
 * @param detected_current_ma 输出参数, ICO 检测到的最大电流 (mA)
 * @return int8_t BQ25710_OK 或错误码
 */
int8_t input_power_run_ico(uint16_t *detected_current_ma);

/**
 * @brief IDPM 与 VDPM 联合工作完整演示
 *
 * 展示5种场景:
 *   场景A: 适配器功率充足 → 正常充电
 *   场景B: 适配器功率不足 → IDPM 自动降流
 *   场景C: 输入电压跌落 → VDPM 介入保护
 *   场景D: CPU Turbo → 峰值功率模式 Level1
 *   场景E: 极端负载 → 峰值功率模式 Level2 (电池补偿)
 */
void input_power_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* BQ25710_EXAMPLE_INPUT_POWER_H */
