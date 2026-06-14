/**
 * @file bq25710_example_prochot.h
 * @brief BQ25710 PROCHOT 多源热保护与CPU降频管理 - 公共接口
 *
 * 提供9个PROCHOT触发源的完整配置、中断服务框架、
 * 脉冲扩展管理及多场景触发恢复演示。
 *
 * @note 基于已修复的 bq25710.h 驱动API
 */

#ifndef BQ25710_EXAMPLE_PROCHOT_H
#define BQ25710_EXAMPLE_PROCHOT_H

#include <stdint.h>
#include "bq25710.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * PROCHOT 触发源位掩码 (对应 ProchotOption1 bits 0-7)
 * ======================================================================== */
#define PROCHOT_SRC_ADAPTER_REMOVAL  (1 << 0)  /**< 适配器移除触发 */
#define PROCHOT_SRC_BATTERY_REMOVAL  (1 << 1)  /**< 电池移除触发 */
#define PROCHOT_SRC_VSYS             (1 << 2)  /**< VSYS 欠压触发 */
#define PROCHOT_SRC_IDCHG            (1 << 3)  /**< 放电过流触发 */
#define PROCHOT_SRC_INOM             (1 << 4)  /**< 输入平均过流触发 */
#define PROCHOT_SRC_ICRIT            (1 << 5)  /**< 输入峰值过流触发 */
#define PROCHOT_SRC_CMPIN            (1 << 6)  /**< 独立比较器触发 */
#define PROCHOT_SRC_VAP              (1 << 7)  /**< 退出 VAP 触发 (需配合 ProchotStatus[8]) */

/** 全部触发源使能 */
#define PROCHOT_SRC_ALL 0xFF

/* ========================================================================
 * PROCHOT 脉冲宽度枚举
 * ======================================================================== */
typedef enum {
    PROCHOT_WIDTH_100US = 0,  /**< 100μs 脉冲 (ProchotStatus[13:12]=00) */
    PROCHOT_WIDTH_1MS   = 1,  /**< 1ms 脉冲   (ProchotStatus[13:12]=01) */
    PROCHOT_WIDTH_10MS  = 2,  /**< 10ms 脉冲  (ProchotStatus[13:12]=10, 默认) */
    PROCHOT_WIDTH_5MS   = 3   /**< 5ms 脉冲   (ProchotStatus[13:12]=11) */
} prochot_pulse_width_t;

/* ========================================================================
 * PROCHOT IDCHG 去抖时间枚举
 * ======================================================================== */
typedef enum {
    IDCHG_DEG_2MS  = 0,  /**< 2ms 去抖   (ProchotOption1[9:8]=00) */
    IDCHG_DEG_130US = 1, /**< 130μs 去抖 (ProchotOption1[9:8]=01, 默认) */
    IDCHG_DEG_8MS  = 2,  /**< 8ms 去抖   (ProchotOption1[9:8]=10) */
    IDCHG_DEG_16MS = 3   /**< 16ms 去抖  (ProchotOption1[9:8]=11) */
} prochot_idchg_deg_t;

/* ========================================================================
 * ICRIT 去抖时间枚举
 * ======================================================================== */
typedef enum {
    ICRIT_DEG_15US  = 0,  /**< 15μs 去抖  (ProchotOption0[10:9]=00) */
    ICRIT_DEG_120US = 1,  /**< 120μs 去抖 (ProchotOption0[10:9]=01, 默认) */
    ICRIT_DEG_500US = 2,  /**< 500μs 去抖 (ProchotOption0[10:9]=10) */
    ICRIT_DEG_1MS   = 3   /**< 1ms 去抖   (ProchotOption0[10:9]=11) */
} prochot_icrit_deg_t;

/* ========================================================================
 * ILIM2 阈值 (适配器峰值过载百分比)
 * ======================================================================== */
typedef enum {
    ILIM2_110 = 1,   /**< ILIM2 = 110% IIN_HOST (ProchotOption0[15:11]=00001) */
    ILIM2_120 = 2,   /**< ILIM2 = 120% IIN_HOST */
    ILIM2_130 = 3,   /**< ILIM2 = 130% IIN_HOST */
    ILIM2_140 = 4,   /**< ILIM2 = 140% IIN_HOST */
    ILIM2_150 = 5,   /**< ILIM2 = 150% IIN_HOST (默认, ProchotOption0[15:11]=01001) */
    ILIM2_160 = 6,   /**< ILIM2 = 160% IIN_HOST */
    ILIM2_170 = 7,   /**< ILIM2 = 170% IIN_HOST */
    ILIM2_180 = 8,   /**< ILIM2 = 180% IIN_HOST */
    ILIM2_190 = 9,   /**< ILIM2 = 190% IIN_HOST */
    ILIM2_200 = 10,  /**< ILIM2 = 200% IIN_HOST */
    ILIM2_210 = 11,  /**< ILIM2 = 210% IIN_HOST */
    ILIM2_220 = 12,  /**< ILIM2 = 220% IIN_HOST */
    ILIM2_230 = 13   /**< ILIM2 = 230% IIN_HOST */
} prochot_ilim2_t;

/* ========================================================================
 * PROCHOT 事件结构体
 * ======================================================================== */
typedef struct {
    uint8_t  adapter_removal : 1;  /**< STAT_Adapter_Removal */
    uint8_t  battery_removal : 1;  /**< STAT_Battery_Removal */
    uint8_t  vsys_under      : 1;  /**< STAT_VSYS */
    uint8_t  idchg_over      : 1;  /**< STAT_IDCHG */
    uint8_t  inom_over       : 1;  /**< STAT_INOM */
    uint8_t  icrit_over      : 1;  /**< STAT_ICRIT */
    uint8_t  comp_trigger    : 1;  /**< STAT_COMP */
    uint8_t  vdpm_under      : 1;  /**< STAT_VDPM */
    uint8_t  exit_vap        : 1;  /**< STAT_EXIT_VAP */
    uint8_t  vap_fail        : 1;  /**< STAT_VAP_FAIL */
} prochot_event_t;

/* ========================================================================
 * PROCHOT 配置结构体
 * ======================================================================== */
typedef struct {
    uint8_t              trigger_mask;       /**< 触发源使能掩码 */
    prochot_pulse_width_t pulse_width;       /**< 脉冲宽度 */
    uint8_t              pulse_extend;       /**< 1=脉冲扩展模式 (保持低直到主机清除) */
    uint16_t             idchg_threshold_ma; /**< IDCHG 放电过流阈值 (mA, 步进512, 偏移+128) */
    prochot_idchg_deg_t  idchg_deg;          /**< IDCHG 去抖时间 */
    prochot_icrit_deg_t  icrit_deg;          /**< ICRIT 去抖时间 */
    prochot_ilim2_t      ilim2;              /**< ILIM2 峰值过载百分比 */
    uint8_t              inom_deg_50ms;      /**< INOM 去抖: 0=1ms, 1=50ms */
} prochot_config_t;

/* ========================================================================
 * 公共 API 声明
 * ======================================================================== */

/**
 * @brief 初始化 PROCHOT 完整配置
 *
 * 配置流程:
 *   1. 设置触发源屏蔽 (ProchotOption1 bits 0-7)
 *   2. 设置 IDCHG_VTH 放电过流阈值
 *   3. 设置 IDCHG_DEG / ICRIT_DEG 去抖时间
 *   4. 设置 ILIM2_VTH 峰值电流百分比
 *   5. 设置 PROCHOT_WIDTH 脉冲宽度
 *   6. 设置 EN_PROCHOT_EXIT 脉冲扩展
 *   7. 配置 INOM_DEG 去抖
 *
 * @param cfg  PROCHOT 配置结构体指针
 * @return int8_t BQ25710_OK 或错误码
 */
int8_t prochot_init(const prochot_config_t *cfg);

/**
 * @brief 读取并解析 PROCHOT 触发状态
 *
 * 读取 ProchotStatus 寄存器并提取各事件位。
 *
 * @param event 输出参数, 事件结构体指针
 * @return int8_t BQ25710_OK 或错误码
 */
int8_t prochot_read_events(prochot_event_t *event);

/**
 * @brief 清除所有 PROCHOT 状态位并释放引脚
 *
 * 写 0 至 ProchotStatus bits 0-9 和 bit11 (PROCHOT_CLEAR)
 *
 * @return int8_t BQ25710_OK 或错误码
 */
int8_t prochot_clear_and_release(void);

/**
 * @brief PROCHOT 中断服务例程 (ISR) 框架
 *
 * 在 GPIO 中断中调用此函数:
 *   1. 读取 ProchotStatus 识别触发源
 *   2. 记录事件到 log
 *   3. 执行对应降频策略
 *   4. 清除状态位释放 PROCHOT 引脚
 *
 * @note 此函数不包含硬件 GPIO 初始化，仅处理寄存器层面逻辑
 */
void prochot_isr_handler(void);

/**
 * @brief 模拟多场景触发测试
 *
 * 此函数展示以下场景:
 *   - 场景A: 适配器移除 → PROCHOT 触发 → CPU 降频 → 适配器恢复 → 清除
 *   - 场景B: 电池放电过流 → IDCHG 超阈值 → PROCHOT 触发 → 降频 → 恢复
 *   - 场景C: VSYS 电压下冲 → PROCHOT 触发 → VAP 补偿 → 恢复
 */
void prochot_scenario_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* BQ25710_EXAMPLE_PROCHOT_H */
