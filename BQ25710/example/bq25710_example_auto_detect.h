/**
 * @file bq25710_example_auto_detect.h
 * @brief BQ25710 多电池类型自动识别与自适应配置 - 公共接口
 *
 * 自动识别电池类型和节数, 自动配置对应充电参数。
 *
 * 识别策略:
 *
 * 1. CELL_BATPRESZ 引脚电压检测节数:
 *    通过电阻分压网络, 不同节数输出不同电压。
 *    芯片内部比较器根据电压自动设定初始化值:
 *    - 1S: 0.0V ~ 0.5V
 *    - 2S: 0.5V ~ 1.0V
 *    - 3S: 1.0V ~ 1.5V
 *    - 4S: 1.5V ~ 2.0V
 *
 * 2. 充电特性判断电池化学类型 (LiFePO4 vs NMC):
 *    - 读取 Default ChargeVoltage 寄存器默认值
 *    - 写小电流充电 (如 256mA)
 *    - 观察电池电压平台:
 *      NMC: 3.6~3.7V/cell (标称)
 *      LiFePO4: 3.2~3.3V/cell (标称)
 *    - 记录电压平台, 判别类型
 *
 * 3. 热插拔自适应:
 *    - CELL_BATPRESZ 拉高 → 电池移除
 *    - 适配器保持存在
 *    - 新电池插入 → 重新识别 + 配置
 *
 * @note 基于已修复的 bq25710.h 驱动API
 */

#ifndef BQ25710_EXAMPLE_AUTO_DETECT_H
#define BQ25710_EXAMPLE_AUTO_DETECT_H

#include <stdint.h>
#include "bq25710.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * 电池类型枚举
 * ======================================================================== */

typedef enum {
    BATTERY_TYPE_UNKNOWN = 0,  /**< 未知类型 */
    BATTERY_TYPE_NMC,          /**< NMC / LiCoO2 / 三元锂 (3.7V 标称) */
    BATTERY_TYPE_LIFEPO4,      /**< LiFePO4 磷酸铁锂 (3.2V 标称) */
} battery_chemistry_t;

/* ========================================================================
 * 电池节数枚举
 * ======================================================================== */

typedef enum {
    CELL_COUNT_UNKNOWN = 0,
    CELL_COUNT_1S = 1,
    CELL_COUNT_2S = 2,
    CELL_COUNT_3S = 3,
    CELL_COUNT_4S = 4,
} cell_count_t;

/* ========================================================================
 * 电池参数结构体
 * ======================================================================== */

typedef struct {
    battery_chemistry_t chemistry;   /**< 电池化学类型 */
    cell_count_t        cells;       /**< 串联节数 */
    uint16_t            charge_voltage_mv;    /**< 充电终止电压 (mV) */
    uint16_t            min_sys_voltage_mv;   /**< 最小系统电压 (mV) */
    uint16_t            charge_current_ma;    /**< 充电电流 (mA) */
    uint16_t            precharge_threshold_mv; /**< 预充电阈值 (mV) */
    uint16_t            termination_current_ma; /**< 充电终止电流 (mA) */
    uint16_t            sysovp_threshold_mv;  /**< SYSOVP 阈值 (mV) */
} battery_params_t;

/* ========================================================================
 * CELL_BATPRESZ 电压范围 (用于节数识别)
 *
 * 芯片内部检测引脚电压自动判定:
 *   1S: V_cell_pin < 0.5V
 *   2S: 0.5V ≤ V_cell_pin < 1.0V
 *   3S: 1.0V ≤ V_cell_pin < 1.5V
 *   4S: 1.5V ≤ V_cell_pin ≤ 2.0V
 * ======================================================================== */

#define CELL_DETECT_1S_MAX_MV  500   /**< 1S 上限 */
#define CELL_DETECT_2S_MAX_MV  1000  /**< 2S 上限 */
#define CELL_DETECT_3S_MAX_MV  1500  /**< 3S 上限 */
#define CELL_DETECT_4S_MAX_MV  2000  /**< 4S 上限 */

/* ========================================================================
 * 电池化学类型判断参数
 *
 * 通过充电电压平台判断:
 *   NMC: 标称 3.7V, 充电过程中电压快速上升至 3.5V+ 后缓慢上升
 *   LiFePO4: 标称 3.2V, 充电过程中电压在 3.2~3.3V 有长平台
 *
 * 判据:
 *   在 256mA 小电流充电 30s 后读 VBAT:
 *     V_per_cell > 3.5V → NMC
 *     V_per_cell < 3.3V → LiFePO4
 *     V_per_cell ∈ [3.3, 3.5] → 继续充电观察
 * ======================================================================== */

/** 识别充电电流: 256mA (小电流安全充电) */
#define DETECT_CHARGE_CURRENT_MA    256

/** NMC 判据: 充电30s 后每节电压 > 3.5V */
#define DETECT_NMC_THRESHOLD_MV_PER_CELL  3500

/** LiFePO4 判据: 充电30s 后每节电压 < 3.3V */
#define DETECT_LFP_THRESHOLD_MV_PER_CELL  3300

/** 识别充电持续时间 (ms) */
#define DETECT_CHARGE_DURATION_MS  30000

/* ========================================================================
 * 公共 API 声明
 * ======================================================================== */

/**
 * @brief 自动检测电池节数
 *
 * 读取 MaxChargeVoltage 寄存器当前值,
 * 根据芯片自动检测的电池节数, 或通过 CELL_BATPRESZ
 * 关联逻辑推断。
 *
 * 注意: BQ25710 上电复位后自动根据 CELL_BATPRESZ 电压
 * 设置默认充放电参数。
 *
 * @param cells 输出参数: 检测到的电池节数
 * @return int8_t
 */
int8_t detect_cell_count(cell_count_t *cells);

/**
 * @brief 自动识别电池化学类型
 *
 * 流程:
 *   1. 确认电池存在且电压在安全范围
 *   2. 写 256mA 小电流充电
 *   3. 使能充电 30 秒
 *   4. 读取 VBAT, 计算 V_per_cell
 *   5. 根据判据确定 NMC 或 LiFePO4
 *   6. 停止充电
 *
 * @param chemistry 输出参数: 检测到的电池类型
 * @param cells     已检测到的节数
 * @return int8_t
 */
int8_t detect_battery_chemistry(battery_chemistry_t *chemistry,
                                cell_count_t cells);

/**
 * @brief 根据检测结果自动配置充电参数
 *
 * 查表选择:
 *   - ChargeVoltage: NMC=4.2V/cell, LFP=3.6V/cell
 *   - MinSystemVoltage: NMC≈3.5V/cell, LFP≈3.0V/cell
 *   - ChargeCurrent: 根据节数推荐 (1S=1A, 2S=2A, 3S+=2A)
 *   - SYSOVP: 1S=5V, 2S=12V, 3S/4S=19.5V
 *
 * @param params 电池参数结构体指针 (输入 chemistry + cells, 输出全参数)
 * @return int8_t
 */
int8_t auto_config_params(battery_params_t *params);

/**
 * @brief 应用电池参数到 BQ25710
 *
 * 写入:
 *   1. MaxChargeVoltage (ChargeVoltage)
 *   2. MinSystemVoltage
 *   3. ChargeCurrent
 *   4. InputCurrentLimit (建议值)
 *   5. VINDPM (建议值)
 *
 * @param params 电池参数结构体指针
 * @return int8_t
 */
int8_t apply_battery_params(const battery_params_t *params);

/**
 * @brief 电池热插拔检测与自动恢复
 *
 * 通过轮询 CELL_BATPRESZ 或 ChargerStatus 检测电池状态.
 *
 * 流程:
 *   1. 检测电池移除 (CELL_BATPRESZ=0 or BATPRES 触发)
 *   2. 停止充电
 *   3. 等待适配器供电稳定
 *   4. 检测新电池插入
 *   5. 重新识别类型和节数
 *   6. 自动应用参数并启动充电
 *
 * @param params 输出参数: 新电池的参数
 * @return int8_t
 */
int8_t auto_hotswap_recover(battery_params_t *params);

/**
 * @brief 完整电池自动识别演示
 *
 * 演示:
 *   1. 检测节数
 *   2. 识别化学类型
 *   3. 自动配置参数
 *   4. 应用参数开始充电
 *   5. 热插拔恢复流程
 */
void auto_detect_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* BQ25710_EXAMPLE_AUTO_DETECT_H */
