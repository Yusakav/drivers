/**
 * @file    bq25710_charge_example.h
 * @author  nyarukov (luckychaoyue1@gmail.com)
 * @brief   BQ25710 充电配置与应用示例 —— 头文件
 * @version 1.0
 * @date    2026-06-11
 *
 * @details 本文件定义了磷酸铁锂 (LiFePO4) 与三元锂 (NMC) 电池在 1S~4S
 *          配置下的全部充电参数、充电阶段状态机枚举、OTG 预定义电压等级
 *          以及应用层 API 声明。
 *
 *          硬件依赖：CELL_BATPRESZ 引脚外部分压电阻需与目标电芯节数匹配，
 *          否则 SYSOVP / BATLOWV 阈值将不正确。
 *
 * @copyright Copyright (c) 2026
 */

#ifndef BQ25710_CHARGE_EXAMPLE_H
#define BQ25710_CHARGE_EXAMPLE_H

#include <stdint.h>
#include "bq25710.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ================================================================
 *  电池参数定义表 (Battery Parameter Table)
 *
 *  以分组宏定义形式覆盖 1S~4S 磷酸铁锂 / 三元锂的完整参数集。
 *  命名规则: BQ25710_PARAM_{S}x_{化学类型}_{参数名}
 *
 *  ┌──────────┬──────┬──────────────┬───────────┬──────────┬──────────┬──────────┐
 *  │ 电池类型  │ 节数 │ 充电终止电压  │ 总充电电压 │ 最小系统 │ 预充电阈值│ 典型充电 │
 *  │          │      │ /cell        │           │ 电压     │          │ 电流     │
 *  ├──────────┼──────┼──────────────┼───────────┼──────────┼──────────┼──────────┤
 *  │ LiFePO4  │ 1S   │ 3.60V        │ 3.600V    │ 3.072V   │ 3.0V     │ 1A       │
 *  │ LiFePO4  │ 2S   │ 3.60V        │ 7.200V    │ 6.144V   │ 6.0V     │ 2A       │
 *  │ LiFePO4  │ 3S   │ 3.60V        │ 10.800V   │ 9.216V   │ 9.0V     │ 2A       │
 *  │ LiFePO4  │ 4S   │ 3.60V        │ 14.400V   │ 12.288V  │ 12.0V    │ 2A       │
 *  │ Li-ion   │ 1S   │ 4.20V        │ 4.200V    │ 3.584V   │ 3.0V     │ 1A       │
 *  │ (NMC)    │ 2S   │ 4.20V        │ 8.400V    │ 7.168V   │ 6.0V     │ 2A       │
 *  │          │ 3S   │ 4.20V        │ 12.600V   │ 10.752V  │ 9.0V     │ 2A       │
 *  │          │ 4S   │ 4.20V        │ 16.800V   │ 14.336V  │ 12.0V    │ 2A       │
 *  └──────────┴──────┴──────────────┴───────────┴──────────┴──────────┴──────────┘
 *
 *  充电终止判断: 当 CV 阶段电流降至典型充电电流的 10% (C/10) 时认为充满。
 *  预充电电流: 硬件自动钳位 (1S≈384mA LDO 模式; 2S~4S 由硬件限制)。
 * ================================================================ */

/* -------- LiFePO4 (磷酸铁锂) 参数 -------- */
/* 1S LiFePO4 */
#define BQ25710_PARAM_1S_LFP_VREG_PER_CELL_MV       3600
#define BQ25710_PARAM_1S_LFP_CHARGE_VOLTAGE_MV      3600
#define BQ25710_PARAM_1S_LFP_MIN_SYS_VOLTAGE_MV     3072
#define BQ25710_PARAM_1S_LFP_BATLOWV_MV             3000
#define BQ25710_PARAM_1S_LFP_CHARGE_CURRENT_MA      1024
#define BQ25710_PARAM_1S_LFP_TERM_CURRENT_MA        100     /* C/10 = 100mA */
#define BQ25710_PARAM_1S_LFP_SYSOVP_MV              5000

/* 2S LiFePO4 */
#define BQ25710_PARAM_2S_LFP_VREG_PER_CELL_MV       3600
#define BQ25710_PARAM_2S_LFP_CHARGE_VOLTAGE_MV      7200
#define BQ25710_PARAM_2S_LFP_MIN_SYS_VOLTAGE_MV     6144
#define BQ25710_PARAM_2S_LFP_BATLOWV_MV             6000
#define BQ25710_PARAM_2S_LFP_CHARGE_CURRENT_MA      2000
#define BQ25710_PARAM_2S_LFP_TERM_CURRENT_MA        200     /* C/10 = 200mA */
#define BQ25710_PARAM_2S_LFP_SYSOVP_MV              12000

/* 3S LiFePO4 */
#define BQ25710_PARAM_3S_LFP_VREG_PER_CELL_MV       3600
#define BQ25710_PARAM_3S_LFP_CHARGE_VOLTAGE_MV      10800
#define BQ25710_PARAM_3S_LFP_MIN_SYS_VOLTAGE_MV     9216
#define BQ25710_PARAM_3S_LFP_BATLOWV_MV             9000
#define BQ25710_PARAM_3S_LFP_CHARGE_CURRENT_MA      2000
#define BQ25710_PARAM_3S_LFP_TERM_CURRENT_MA        200     /* C/10 = 200mA */
#define BQ25710_PARAM_3S_LFP_SYSOVP_MV              19500

/* 4S LiFePO4 */
#define BQ25710_PARAM_4S_LFP_VREG_PER_CELL_MV       3600
#define BQ25710_PARAM_4S_LFP_CHARGE_VOLTAGE_MV      14400
#define BQ25710_PARAM_4S_LFP_MIN_SYS_VOLTAGE_MV     12288
#define BQ25710_PARAM_4S_LFP_BATLOWV_MV             12000
#define BQ25710_PARAM_4S_LFP_CHARGE_CURRENT_MA      2000
#define BQ25710_PARAM_4S_LFP_TERM_CURRENT_MA        200     /* C/10 = 200mA */
#define BQ25710_PARAM_4S_LFP_SYSOVP_MV              19500

/* -------- Li-ion NMC (三元锂) 参数 -------- */
/* 1S NMC */
#define BQ25710_PARAM_1S_NMC_VREG_PER_CELL_MV       4200
#define BQ25710_PARAM_1S_NMC_CHARGE_VOLTAGE_MV      4200
#define BQ25710_PARAM_1S_NMC_MIN_SYS_VOLTAGE_MV     3584
#define BQ25710_PARAM_1S_NMC_BATLOWV_MV             3000
#define BQ25710_PARAM_1S_NMC_CHARGE_CURRENT_MA      1000
#define BQ25710_PARAM_1S_NMC_TERM_CURRENT_MA        100     /* C/10 = 100mA */
#define BQ25710_PARAM_1S_NMC_SYSOVP_MV              5000

/* 2S NMC */
#define BQ25710_PARAM_2S_NMC_VREG_PER_CELL_MV       4200    /**< 充电终止电压 4.20V */
#define BQ25710_PARAM_2S_NMC_CHARGE_VOLTAGE_MV      8400    /**< 总充电电压 8.400V */
#define BQ25710_PARAM_2S_NMC_MIN_SYS_VOLTAGE_MV     7168    /**< 最小系统电压 7.168V */
#define BQ25710_PARAM_2S_NMC_BATLOWV_MV             6000    /**< 预充电阈值 6.0V */
#define BQ25710_PARAM_2S_NMC_CHARGE_CURRENT_MA      2000    /**< 典型充电电流 2A */
#define BQ25710_PARAM_2S_NMC_TERM_CURRENT_MA        200     /* C/10 = 200mA */
#define BQ25710_PARAM_2S_NMC_SYSOVP_MV              

/* 3S NMC */
#define BQ25710_PARAM_3S_NMC_VREG_PER_CELL_MV       4200
#define BQ25710_PARAM_3S_NMC_CHARGE_VOLTAGE_MV      12600
#define BQ25710_PARAM_3S_NMC_MIN_SYS_VOLTAGE_MV     10752
#define BQ25710_PARAM_3S_NMC_BATLOWV_MV             9000
#define BQ25710_PARAM_3S_NMC_CHARGE_CURRENT_MA      2000
#define BQ25710_PARAM_3S_NMC_TERM_CURRENT_MA        200     /* C/10 = 200mA */
#define BQ25710_PARAM_3S_NMC_SYSOVP_MV              19500

/* 4S NMC */
#define BQ25710_PARAM_4S_NMC_VREG_PER_CELL_MV       4200
#define BQ25710_PARAM_4S_NMC_CHARGE_VOLTAGE_MV      16800
#define BQ25710_PARAM_4S_NMC_MIN_SYS_VOLTAGE_MV     14336
#define BQ25710_PARAM_4S_NMC_BATLOWV_MV             12000
#define BQ25710_PARAM_4S_NMC_CHARGE_CURRENT_MA      2000
#define BQ25710_PARAM_4S_NMC_TERM_CURRENT_MA        200     /* C/10 = 200mA */
#define BQ25710_PARAM_4S_NMC_SYSOVP_MV              19500


/* ================================================================
 *  2. 枚举定义
 * ================================================================ */

/** @brief 电池化学类型 */
typedef enum {
    BQ25710_BATTERY_TYPE_LIFEPO4 = 0,   /**< 磷酸铁锂 (LiFePO4)   */
    BQ25710_BATTERY_TYPE_NMC     = 1,   /**< 三元锂 (NMC / Li-ion) */
} bq25710_battery_type_t;

/** @brief 电芯串联节数 (由 CELL_BATPRESZ 硬件分压决定) */
typedef enum {
    BQ25710_CELL_COUNT_1S = 1,          /**< 1 节串联 */
    BQ25710_CELL_COUNT_2S = 2,          /**< 2 节串联 */
    BQ25710_CELL_COUNT_3S = 3,          /**< 3 节串联 */
    BQ25710_CELL_COUNT_4S = 4,          /**< 4 节串联 */
} batt_cell_count_t;

/** @brief 充电阶段枚举 (软件状态机) */
typedef enum {
    BQ25710_CHARGE_STAGE_IDLE           = 0,  /**< 空闲，等待充电指令       */
    BQ25710_CHARGE_STAGE_DETECTION      = 1,  /**< 阶段0: 检测与识别         */
    BQ25710_CHARGE_STAGE_PRECHARGE      = 2,  /**< 阶段1: 预充电 (亏电)      */
    BQ25710_CHARGE_STAGE_FAST_CHARGE_CC = 3,  /**< 阶段2: 恒流快充 (CC)      */
    BQ25710_CHARGE_STAGE_FAST_CHARGE_CV = 4,  /**< 阶段3: 恒压充电 (CV)      */
    BQ25710_CHARGE_STAGE_DONE           = 5,  /**< 阶段4: 充电完成           */
    BQ25710_CHARGE_STAGE_FAULT          = 6,  /**< 阶段5: 故障处理           */
} bq25710_charge_stage_t;

/** @brief OTG 输出电压预设 */
typedef enum {
    BQ25710_OTG_VOUT_5V  = 5000,        /**< USB PD 5V  */
    BQ25710_OTG_VOUT_9V  = 9000,        /**< USB PD 9V  */
    BQ25710_OTG_VOUT_12V = 12000,       /**< USB PD 12V */
    BQ25710_OTG_VOUT_15V = 15000,       /**< USB PD 15V */
    BQ25710_OTG_VOUT_20V = 20000,       /**< USB PD 20V */
} bq25710_otg_vout_preset_t;

/** @brief OTG 输出电流预设 */
typedef enum {
    BQ25710_OTG_IOUT_500MA  = 500,      /**< USB Type-C 500mA  */
    BQ25710_OTG_IOUT_1500MA = 1500,     /**< USB Type-C 1.5A   */
    BQ25710_OTG_IOUT_3000MA = 3000,     /**< USB Type-C 3A     */
    BQ25710_OTG_IOUT_5000MA = 5000,     /**< USB Type-C 5A     */
} bq25710_otg_iout_preset_t;


/* ================================================================
 *  3. 充电运行上下文结构体
 * ================================================================ */

/**
 * @brief 电池参数包 (一组电芯配置下的全部充电参数)
 */
typedef struct {
    uint16_t vreg_per_cell_mv;       /**< 单节电芯充电终止电压 (mV)           */
    uint16_t charge_voltage_mv;      /**< 总充电电压 = vreg_per_cell × cells  */
    uint16_t min_sys_voltage_mv;     /**< 最小系统电压 (mV)                    */
    uint16_t batlowv_mv;             /**< 预充电阈值 BATLOWV (mV)              */
    uint16_t charge_current_ma;      /**< 典型充电电流 (mA)                    */
    uint16_t term_current_ma;        /**< 充电终止电流 = C/10 (mA)             */
    uint16_t sysovp_mv;             /**< 系统过压保护阈值 (mV)                 */
} bq25710_battery_params_t;

/**
 * @brief 充电运行上下文
 *
 * 维护一次完整充电周期中所有状态变量，由状态机函数 bq25710_charge_run()
 * 周期性驱动。
 */
typedef struct {
    /* --- 电池配置 --- */
    bq25710_battery_type_t  battery_type;   /**< 电池化学类型         */
    batt_cell_count_t    cell_count;     /**< 电芯串联节数         */
    bq25710_battery_params_t params;        /**< 当前生效的电池参数   */

    /* --- 充电阶段 --- */
    bq25710_charge_stage_t  stage;          /**< 当前充电阶段         */
    bq25710_charge_stage_t  prev_stage;     /**< 上一充电阶段         */

    /* --- 实时监测数据 --- */
    BQ25710_ChargerStatus_t chg_status;     /**< ChargerStatus 寄存器 */
    BQ25710_ADC_Result_t    adc;            /**< ADC 采样结果         */
    uint16_t                fault_mask;     /**< 故障位掩码           */

    /* --- 看门狗喂狗 --- */
    uint32_t                last_wdt_feed_ms; /**< 上次喂狗时间戳 (ms) */

    /* --- 阶段耗时统计 --- */
    uint32_t                stage_enter_ms; /**< 进入当前阶段时刻 (ms) */
    uint32_t                total_charge_ms;/**< 累计充电时间 (ms)     */

    /* --- 错误跟踪 --- */
    int8_t                  last_error;     /**< 最后一次 API 错误码  */
    uint8_t                 fault_retry_cnt;/**< 故障重试计数          */
} bq25710_charge_ctx_t;


/* ================================================================
 *  4. API 函数声明
 * ================================================================ */

/* ---------- 初始化与参数查询 ---------- */

/**
 * @brief 根据电池类型和节数获取对应的参数包
 *
 * @param battery_type  电池化学类型
 * @param cell_count    电芯节数
 * @param params        输出: 参数包指针
 * @return int8_t       BQ25710_OK 或 BQ25710_ERR_INVALID_PARAM
 */
int8_t bq25710_get_battery_params(bq25710_battery_type_t  battery_type,
                                  batt_cell_count_t    cell_count,
                                  bq25710_battery_params_t *params);

/**
 * @brief 初始化充电上下文并配置充电器硬件
 *
 * 执行顺序:
 *   1. 根据 battery_type + cell_count 查询参数包
 *   2. 设置 MaxChargeVoltage / MinSystemVoltage
 *   3. 配置看门狗 (88s 超时)
 *   4. 禁用充电 (CHRG_INHIBIT=1), 等待后续启动指令
 *   5. 使能 ADC 连续采样 (VBAT/VSYS/ICHG/IIN 通道)
 *   6. 进入 DETECTION 阶段
 *
 * @param ctx           充电上下文指针 (由调用者分配)
 * @param battery_type  电池化学类型
 * @param cell_count    电芯节数
 * @param now_ms        当前系统毫秒时间戳
 * @return int8_t       状态码
 */
int8_t bq25710_charge_init(bq25710_charge_ctx_t  *ctx,
                           bq25710_battery_type_t  battery_type,
                           batt_cell_count_t    cell_count,
                           uint32_t                now_ms);

/* ---------- 充电状态机 ---------- */

/**
 * @brief 充电状态机主循环 (需周期性调用，推荐周期 500ms~1s)
 *
 * 每次调用执行当前阶段的逻辑，必要时触发阶段迁移。
 * 阶段迁移规则:
 *   DETECTION  → VBAT < BATLOWV → PRECHARGE
 *              → VBAT ≥ BATLOWV → FAST_CHARGE_CC
 *   PRECHARGE  → VBAT ≥ BATLOWV → FAST_CHARGE_CC
 *   CC         → VBAT ≥ VREG × 98% → CV
 *   CV         → ICHG ≤ C/10 → DONE
 *   任意阶段   → 故障位非零 → FAULT
 *   FAULT      → 故障清除 → 回到原阶段
 *
 * @param ctx     充电上下文指针
 * @param now_ms  当前系统毫秒时间戳
 * @return bq25710_charge_stage_t  执行后的当前阶段
 */
bq25710_charge_stage_t bq25710_charge_run(bq25710_charge_ctx_t *ctx,
                                          uint32_t               now_ms);

/* ---------- 状态检测 ---------- */

/**
 * @brief 综合状态检测函数
 *
 * @param ctx   充电上下文指针
 * @param now_ms 当前毫秒时间戳
 *
 * 内部执行:
 *   - 读取 ChargerStatus (AC 状态、充电阶段、DPM 状态)
 *   - 读取 ADC (VBAT、VSYS、ICHG、IIN)
 *   - 读取故障掩码
 *   - 更新 ctx 内对应字段
 */
void bq25710_battery_status_monitor(bq25710_charge_ctx_t *ctx,
                                    uint32_t               now_ms);

/**
 * @brief 打印所有关键状态信息
 *
 * 输出内容:
 *   - AC 适配器状态
 *   - 当前充电阶段 (文字描述)
 *   - VBAT / VSYS / VBUS 电压
 *   - ICHG / IIN 电流
 *   - 故障标志位列表
 *   - 充电累计时间
 *
 * @param ctx 充电上下文指针
 */
void bq25710_print_status(const bq25710_charge_ctx_t *ctx);

/**
 * @brief 判断是否正在充电 (预充或快充)
 *
 * @param ctx 充电上下文指针
 * @return int 1=正在充电, 0=未充电
 */
int bq25710_is_charging_ex(const bq25710_charge_ctx_t *ctx);

/**
 * @brief 判断充电是否已完成
 *
 * @param ctx 充电上下文指针
 * @return int 1=充电完成, 0=未完成
 */
int bq25710_is_charge_done_ex(const bq25710_charge_ctx_t *ctx);

/* ---------- OTG 反向输出 ---------- */

/**
 * @brief 使能 5V OTG 输出 (USB PD 5V)
 *
 * @param current_ma 输出电流限制 (mA), 典型 3000
 * @return int8_t    状态码
 */
int8_t bq25710_otg_enable_5v(uint16_t current_ma);

/**
 * @brief 使能 9V OTG 输出 (USB PD 9V)
 *
 * @param current_ma 输出电流限制 (mA), 典型 3000
 * @return int8_t    状态码
 */
int8_t bq25710_otg_enable_9v(uint16_t current_ma);

/**
 * @brief 使能 12V OTG 输出 (USB PD 12V)
 *
 * @param current_ma 输出电流限制 (mA), 典型 3000
 * @return int8_t    状态码
 */
int8_t bq25710_otg_enable_12v(uint16_t current_ma);

/**
 * @brief 使能 15V OTG 输出 (USB PD 15V)
 *
 * @param current_ma 输出电流限制 (mA), 典型 3000
 * @return int8_t    状态码
 */
int8_t bq25710_otg_enable_15v(uint16_t current_ma);

/**
 * @brief 使能 20V OTG 输出 (USB PD 20V)
 *
 * @param current_ma 输出电流限制 (mA), 典型 3000
 * @return int8_t    状态码
 */
int8_t bq25710_otg_enable_20v(uint16_t current_ma);

/**
 * @brief 关闭 OTG 模式，恢复充电模式
 *
 * @return int8_t 状态码
 */
int8_t bq25710_otg_disable(void);


#ifdef __cplusplus
}
#endif

#endif /* BQ25710_CHARGE_EXAMPLE_H */
