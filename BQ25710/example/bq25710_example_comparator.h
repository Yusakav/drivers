/**
 * @file bq25710_example_comparator.h
 * @brief BQ25710 独立比较器应用 (CMPIN/CMPOUT) - 公共接口
 *
 * BQ25710 内置一个独立模拟比较器, 可用于:
 *   - 电池过温保护 (NTC 热敏电阻 + CMPIN)
 *   - 适配器过压检测 (电阻分压 + CMPIN)
 *   - 任意外部电压比较触发
 *
 * 比较器配置项 (ChargeOption1):
 *   CMP_REF[7]:  参考电压: 0=2.3V, 1=1.2V
 *   CMP_POL[6]:  输出极性: 0=反相 (V_CMPIN > V_REF → 低电平)
 *                             1=同相 (V_CMPIN < V_REF → 低电平)
 *   CMP_DEG[5:4]: 消抖时间: 00=禁用, 01=1μs, 10=2ms, 11=5s
 *
 * 比较器输出可:
 *   - 触发 PROCHOT (ProchotOption1 bit[6] PP_COMP)
 *   - 触发强制关闭功率路径 (ChargeOption1 bit[3] FORCE_LATCHOFF)
 *   - 作为 ChargerStatus 状态位报告
 *   - ADC_CMPIN 通道数字读取
 *
 * @note 基于已修复的 bq25710.h 驱动API
 */

#ifndef BQ25710_EXAMPLE_COMPARATOR_H
#define BQ25710_EXAMPLE_COMPARATOR_H

#include <stdint.h>
#include "bq25710.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================
 * 比较器参考电压
 * ======================================================================== */

#define CMP_REF_2V3      0  /**< 内部参考 2.3V */
#define CMP_REF_1V2      1  /**< 内部参考 1.2V */

/* ========================================================================
 * 比较器输出极性
 *
 * CMP_POL=0 (反相/默认): V_CMPIN > V_REF → CMPOUT 输出低
 * CMP_POL=1 (同相):      V_CMPIN < V_REF → CMPOUT 输出低
 * ======================================================================== */

#define CMP_POL_INVERT   0  /**< 反相: VIN>VREF → OUT=L */
#define CMP_POL_NONINV   1  /**< 同相: VIN<VREF → OUT=L */

/* ========================================================================
 * 比较器消抖时间
 * ======================================================================== */

typedef enum {
    CMP_DEG_OFF   = 0,  /**< 无消抖 (直通) */
    CMP_DEG_1US   = 1,  /**< 1 μs 消抖 */
    CMP_DEG_2MS   = 2,  /**< 2 ms 消抖 */
    CMP_DEG_5S    = 3,  /**< 5 s 消抖 (超长) */
} cmp_deg_t;

/* ========================================================================
 * 比较器触发动作配置
 * ======================================================================== */

typedef struct {
    uint8_t ref;              /**< 参考电压 (CMP_REF_2V3 / CMP_REF_1V2) */
    uint8_t pol;              /**< 极性 (CMP_POL_INVERT / CMP_POL_NONINV) */
    cmp_deg_t deg;            /**< 消抖时间 */
    uint8_t trigger_prochot;  /**< 触发 PROCHOT (ProchotOption1 PP_COMP) */
    uint8_t latch_power_path; /**< 触发闭锁 (ChargeOption1 FORCE_LATCHOFF) */
    float    threshold_voltage; /**< 外部比较阈值电压 (V), 仅用于参考 */
} cmp_config_t;

/* ========================================================================
 * NTC 热敏电阻过温保护参数
 *
 * 典型电路: NTC (上拉至 VREF 或 VREG) → CMPIN
 *
 * 参考电压 1.2V, 分压比 50% at 60°C:
 *   NTC_60C = R_25C × B(1/333 - 1/298)
 *   例: R_25C=10kΩ, B=3435
 *       R_60C = 10000 × exp(3435×(1/333-1/298)) = ~3.3kΩ
 *   选择上拉 R_pullup = 3.3kΩ:
 *       V_cmpin = 1.2V × 3.3k/(3.3k+3.3k) = 0.6V at 60°C
 *
 *   使用 1.2V 参考 (CMP_REF_1V2):
 *       V_cmpin < 1.2V → 过温, CMPOUT=L (同相模式)
 * ======================================================================== */

#define NTC_R25_OHM         10000  /**< 25°C 电阻 (Ω) */
#define NTC_B_VALUE         3435   /**< B 常数 (NCP15XH103) */

/* ========================================================================
 * 适配器过压检测参数
 *
 * 电阻分压: VBUS → R1 → CMPIN → R2 → GND
 *
 * 参考电压 2.3V, 保护阈值 23V:
 *   V_cmpin = VBUS × R2 / (R1 + R2)
 *   R2/(R1+R2) = 2.3/23 = 0.1
 *   选 R1=90kΩ, R2=10kΩ
 * ======================================================================== */

#define OVP_R1_OHM          90000  /**< 分压上电阻 */
#define OVP_R2_OHM          10000  /**< 分压下电阻 */
#define OVP_THRESHOLD_V     23.0f  /**< 过压阈值 (V) */

/* ========================================================================
 * 公共 API 声明
 * ======================================================================== */

/**
 * @brief 配置独立比较器 (使用 bq25710_config_comparator API)
 *
 * 配置 ChargeOption1 的相关位:
 *   CMP_REF, CMP_POL, CMP_DEG, FORCE_LATCHOFF
 *
 * 配置 ProchotOption1 (如需触发 PROCHOT):
 *   PP_COMP = 1
 *
 * @param config 比较器配置结构体指针
 * @return int8_t
 */
int8_t comparator_init(const cmp_config_t *config);

/**
 * @brief 读取 CMPOUT 状态
 *
 * 从 ProchotStatus bit[6] STAT_COMP 读取比较器触发状态。
 *
 * @param triggered 输出参数: 0=未触发, 1=已触发
 * @return int8_t
 */
int8_t comparator_read_status(uint8_t *triggered);

/**
 * @brief 通过 ADC 读取 CMPIN 电压
 *
 * ADC_CMPIN 通道: 8-bit, LSB=12mV, 全量程 3.06V (ADC_FULLSCALE=1)
 * 或 2.04V (ADC_FULLSCALE=0)
 *
 * @param voltage_mv 输出参数: CMPIN 电压 (mV)
 * @return int8_t
 */
int8_t comparator_read_cmpin_mv(uint16_t *voltage_mv);

/**
 * @brief 应用场景1: 电池过温保护 (NTC 方案)
 *
 * 配置:
 *   - 参考电压 1.2V
 *   - 同相极性 (V_CMPIN < 1.2V → 过温 → CMPOUT=L)
 *   - 2ms 消抖
 *   - 触发 PROCHOT (CPU 降频降温)
 *   - 超温持续 5s → 闭锁功率路径 (FORCE_LATCHOFF)
 *
 * @return int8_t
 */
int8_t comparator_ntc_overtemp_protect(void);

/**
 * @brief 应用场景2: 适配器过压检测
 *
 * 配置:
 *   - 参考电压 2.3V
 *   - 反相极性 (V_CMPIN > 2.3V → 过压 → CMPOUT=L)
 *   - 1μs 消抖 (快速响应)
 *   - 触发 PROCHOT
 *   - 触发强制关闭功率路径
 *
 * @return int8_t
 */
int8_t comparator_adapter_ovp(void);

/**
 * @brief 完整比较器演示
 *
 * 演示:
 *   1. NTC 过温保护配置
 *   2. 适配器过压检测配置
 *   3. 中断处理流程
 *   4. ADC 读取 CMPIN 电压
 */
void comparator_demo(void);

#ifdef __cplusplus
}
#endif

#endif /* BQ25710_EXAMPLE_COMPARATOR_H */
