/**
 * @file bq25710_example_learn.c
 * @brief BQ25710 学习模式与电池容量估算 - 完整实现
 *
 * 学习模式 (LEARN mode) 允许电池在适配器存在时放电,
 * 用于电量计 (Gas Gauge) 的校准流程。
 *
 * 完整流程:
 *   1. 进入学习模式前充满电 (CC+CV)
 *   2. 使能 EN_LEARN, 电池开始放电
 *   3. 在放电过程中通过 ADC 进行库仑计数
 *   4. 放电至截止电压, 记录总容量
 *   5. 退出学习模式, 保存参数
 *
 * @note 基于已修复的 bq25710.h 驱动API
 */

#include "bq25710_example_learn.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * 内部状态变量
 * ======================================================================== */

static uint8_t  s_learn_active = 0;       /**< 学习模式激活标志 */
static uint8_t  s_iadpt_gain   = LEARN_IADPT_GAIN_20X; /**< 当前 IADPT 增益 */
static uint32_t s_learn_timestamp_ms = 0; /**< 学习时间戳 (由外部定时器更新) */

/* ADC LSB 常量 (基于默认增益) */
/* ICHG 通道: 7-bit, 默认 IBAT_GAIN=0(16x) → LSB=64mA, 全量程 8.128A */
#define ADC_ICHG_LSB_MA_16X    64.0f
#define ADC_ICHG_LSB_MA_8X     128.0f

/* IDCHG 通道: 7-bit, 默认 IBAT_GAIN=0(16x) → LSB=256mA, 全量程 32.512A */
#define ADC_IDCHG_LSB_MA_16X   256.0f
#define ADC_IDCHG_LSB_MA_8X    512.0f

/* IIN 通道: 8-bit, 默认 IADPT_GAIN=0(20x) → LSB=50mA */
#define ADC_IIN_LSB_MA_20X     50.0f
#define ADC_IIN_LSB_MA_40X     25.0f

/* VBAT 通道: 8-bit, LSB=64mV, offset=2880mV */
#define ADC_VBAT_LSB_MV        64.0f
#define ADC_VBAT_OFFSET_MV     2880.0f

/* ========================================================================
 * learn_enter - 进入学习模式
 *
 * ChargeOption0 bit[5] EN_LEARN = 1:
 *   电池允许在适配器存在时放电
 *
 * ChargeOption0 bit[4] IADPT_GAIN:
 *   0=20x (默认) → V_IADPT = 20 × I_RAC × R_AC
 *   1=40x        → V_IADPT = 40 × I_RAC × R_AC
 * ======================================================================== */

int8_t learn_enter(uint8_t iadpt_gain)
{
    int8_t ret;

    /* 调用 bq25710_set_learn_mode 使能 EN_LEARN */
    ret = bq25710_set_learn_mode(1);
    if (ret != BQ25710_OK) {
        printf("[Learn] 进入学习模式失败 (ret=%d)\n", ret);
        return ret;
    }

    /* 设置 IADPT 增益 */
    ret = bq25710_set_iadpt_gain(iadpt_gain);
    if (ret != BQ25710_OK) {
        printf("[Learn] IADPT 增益设置失败 (ret=%d)\n", ret);
        bq25710_set_learn_mode(0);
        return ret;
    }

    s_learn_active = 1;
    s_iadpt_gain   = iadpt_gain;
    s_learn_timestamp_ms = 0;

    printf("[Learn] 学习模式已进入:\n");
    printf("  EN_LEARN = 1 (电池可放电)\n");
    printf("  IADPT_GAIN = %dx\n", iadpt_gain == LEARN_IADPT_GAIN_40X ? 40 : 20);

    return BQ25710_OK;
}

/* ========================================================================
 * learn_exit - 退出学习模式
 * ======================================================================== */

int8_t learn_exit(void)
{
    int8_t ret;

    ret = bq25710_set_learn_mode(0);
    if (ret != BQ25710_OK) {
        printf("[Learn] 退出学习模式失败 (ret=%d)\n", ret);
        return ret;
    }

    s_learn_active = 0;
    printf("[Learn] 已退出学习模式 (EN_LEARN=0)\n");
    return BQ25710_OK;
}

/* ========================================================================
 * learn_start_cycle - 开始完整充放电学习循环
 *
 * 充放电流程状态机:
 *
 *   IDLE → CC_CHARGE (I_CHG恒定, V_BAT上升)
 *        → CV_CHARGE (V_REG恒定, I_CHG下降)
 *        → CHARGE_DONE (I_CHG < C/10)
 *        → DISCHARGE  (EN_LEARN, 电池放电)
 *        → DISCHARGE_DONE (V_BAT ≤ 截止电压)
 *        → COMPLETE
 * ======================================================================== */

int8_t learn_start_cycle(learn_session_t *session,
                         uint16_t charge_voltage_mv,
                         uint16_t charge_current_ma,
                         uint16_t discharge_cutoff_mv)
{
    if (!session) return BQ25710_ERR_NULL_PTR;

    int8_t ret;

    /* 清零 session */
    memset(session, 0, sizeof(learn_session_t));

    session->charge_voltage_mv      = charge_voltage_mv;
    session->charge_current_ma      = charge_current_ma;
    session->termination_current_ma = charge_current_ma / 10; /* C/10 终止 */
    session->discharge_cutoff_mv    = discharge_cutoff_mv;

    /*
     * Step 1: 配置充电参数
     *
     * ChargeVoltage: reg = (mV / 8) << 3
     *   例: 3S NMC 12.6V → (12600/8)<<3 = 1575<<3 = 0x3138
     *
     * ChargeCurrent: reg = (mA / 64) << 6
     *   例: 2A → (2000/64)<<6 = 31<<6 = 0x07C0
     */
    ret = bq25710_set_max_charge_voltage(charge_voltage_mv);
    if (ret != BQ25710_OK) return ret;

    ret = bq25710_set_charge_current(charge_current_ma);
    if (ret != BQ25710_OK) return ret;

    /* Step 2: 使能充电 (CC 阶段开始) */
    ret = bq25710_charging_enable(1);
    if (ret != BQ25710_OK) return ret;

    session->stage            = LEARN_STAGE_CC_CHARGE;
    session->stage_timestamp  = 0; /* 由外部定时器设置 */

    printf("[Learn] 充放电循环已启动:\n");
    printf("  V_REG   = %u mV  (reg: 0x%04X)\n",
           charge_voltage_mv, (unsigned)((charge_voltage_mv / 8) << 3));
    printf("  I_CHG   = %u mA  (reg: 0x%04X)\n",
           charge_current_ma, (unsigned)((charge_current_ma / 64) << 6));
    printf("  I_term  = %u mA  (C/10 终止)\n", session->termination_current_ma);
    printf("  V_cut   = %u mV  (放电截止)\n", discharge_cutoff_mv);

    return BQ25710_OK;
}

/* ========================================================================
 * learn_coulomb_count - 库仑计数积分
 *
 * 通过 ADC 周期性采样 IBAT / IADPT 并积分:
 *
 * 充电期间:
 *   - 从 ADC_ICHG 读取实时充电电流
 *   - ΔQ = I_CHG × Δt (累加到 charge_capacity_mah)
 *   - ΔE = I_CHG × V_BAT × Δt (累加到 charge_energy_mwh)
 *
 * 放电期间:
 *   - 从 ADC_IDCHG 读取实时放电电流
 *   - ΔQ = I_DCHG × Δt (累加到 discharge_capacity_mah)
 *   - ΔE = I_DCHG × V_BAT × Δt (累加到 discharge_energy_mwh)
 *
 * @param dt_ms 两次调用间的时间间隔 (ms), 如 100ms
 * ======================================================================== */

int8_t learn_coulomb_count(learn_session_t *session, uint32_t dt_ms)
{
    if (!session) return BQ25710_ERR_NULL_PTR;
    if (dt_ms == 0) return BQ25710_OK;

    int8_t ret;
    float dt_hours = (float)dt_ms / 3600000.0f; /* ms → hours */

    /* 读取 IBAT 电流 ADC */
    BQ25710_ADCIBAT_t adc_ibat;
    int16_t val;
    ret = bq25710_read_word(BQ25710_REG_ADC_IBAT, &val);
    if (ret != BQ25710_OK) return ret;
    adc_ibat.all = (uint16_t)val;

    /* 读取 VBAT 电压 */
    BQ25710_ADCVSYSVBAT_t adc_vbat;
    ret = bq25710_read_word(BQ25710_REG_ADC_VSYS_VBAT, &val);
    if (ret != BQ25710_OK) return ret;
    adc_vbat.all = (uint16_t)val;

    float vbat_mv = (float)adc_vbat.maps.ADC_VBAT * ADC_VBAT_LSB_MV
                    + ADC_VBAT_OFFSET_MV;

    /*
     * 根据当前阶段选择充电或放电积分
     *
     * 注意: ADC 电流 LSB 取决于 IBAT_GAIN 和 IADPT_GAIN 设置。
     * 默认: IBAT_GAIN=0(16x), IADPT_GAIN=0(20x)
     */
    uint8_t ibat_gain;
    bq25710_get_ibat_gain(&ibat_gain);

    switch (session->stage) {
    case LEARN_STAGE_CC_CHARGE:
    case LEARN_STAGE_CV_CHARGE: {
        /* 充电阶段: 使用 ADC_ICHG */
        float ichg_lsb = (ibat_gain == LEARN_IBAT_GAIN_8X)
                         ? ADC_ICHG_LSB_MA_8X : ADC_ICHG_LSB_MA_16X;
        float ichg_ma = (float)adc_ibat.maps.ADC_ICHG * ichg_lsb;

        session->charge_capacity_mah += ichg_ma * dt_hours;
        session->charge_energy_mwh   += ichg_ma * vbat_mv * dt_hours / 1000.0f;
        break;
    }
    case LEARN_STAGE_DISCHARGE: {
        /* 放电阶段: 使用 ADC_IDCHG */
        float idchg_lsb = (ibat_gain == LEARN_IBAT_GAIN_8X)
                          ? ADC_IDCHG_LSB_MA_8X : ADC_IDCHG_LSB_MA_16X;
        float idchg_ma = (float)adc_ibat.maps.ADC_IDCHG * idchg_lsb;

        session->discharge_capacity_mah += idchg_ma * dt_hours;
        session->discharge_energy_mwh   += idchg_ma * vbat_mv * dt_hours / 1000.0f;
        break;
    }
    default:
        break;
    }

    /* 更新阶段持续时间 */
    switch (session->stage) {
    case LEARN_STAGE_CC_CHARGE: session->cc_duration_ms  += dt_ms; break;
    case LEARN_STAGE_CV_CHARGE: session->cv_duration_ms  += dt_ms; break;
    case LEARN_STAGE_DISCHARGE: session->discharge_duration_ms += dt_ms; break;
    default: break;
    }

    return BQ25710_OK;
}

/* ========================================================================
 * learn_calc_efficiency - 充电效率估算
 *
 * 效率 = 放电能量 / 充电能量 × 100%
 *
 * 典型锂电池充电效率: 90%~95%
 *  (含 Buck-Boost 转换器损耗 + 电池化学损耗)
 * ======================================================================== */

int8_t learn_calc_efficiency(learn_session_t *session)
{
    if (!session) return BQ25710_ERR_NULL_PTR;

    if (session->charge_energy_mwh > 0.0f) {
        session->charging_efficiency =
            session->discharge_energy_mwh / session->charge_energy_mwh;
    } else {
        session->charging_efficiency = 0.0f;
    }

    return BQ25710_OK;
}

/* ========================================================================
 * learn_save_params - 保存学习参数
 * ======================================================================== */

int8_t learn_save_params(const learn_session_t *session)
{
    if (!session) return BQ25710_ERR_NULL_PTR;

    /*
     * 实际项目中写入 EEPROM / Flash:
     *
     * typedef struct {
     *     uint16_t design_capacity_mah;   // 设计容量
     *     uint16_t learned_capacity_mah;  // 学习到的实际容量
     *     uint16_t charge_efficiency_pct; // 充电效率 (0~100)
     *     uint16_t charge_voltage_mv;     // 充电终止电压
     *     uint16_t discharge_cutoff_mv;   // 放电截止电压
     * } battery_params_nvm_t;
     *
     * eeprom_write(ADDR_BATTERY_PARAMS, &params, sizeof(params));
     */

    printf("[Learn] 学习参数已保存:\n");
    printf("  充电容量: %.1f mAh\n", session->charge_capacity_mah);
    printf("  放电容量: %.1f mAh\n", session->discharge_capacity_mah);
    printf("  充电效率: %.1f %%\n", session->charging_efficiency * 100.0f);
    printf("  CC 时长:  %lu ms\n", (unsigned long)session->cc_duration_ms);
    printf("  CV 时长:  %lu ms\n", (unsigned long)session->cv_duration_ms);
    printf("  放电时长: %lu ms\n", (unsigned long)session->discharge_duration_ms);

    return BQ25710_OK;
}

/* ========================================================================
 * learn_demo - 完整学习模式演示
 * ======================================================================== */

void learn_demo(void)
{
    int8_t ret;
    learn_session_t session;

    printf("\n========== 学习模式与容量估算演示 ==========\n");

    /*
     * 演示参数: 3S NMC 电池 (11.1V 标称, 12.6V 满电)
     * 充电: CC 2A → CV 12.6V → C/10 终止 (200mA)
     * 放电截止: 9.0V (3.0V/cell)
     */
    const uint16_t demo_vreg   = 12600;  /* 12.6V */
    const uint16_t demo_ichg   = 2000;   /* 2A */
    const uint16_t demo_vcut   = 9000;   /* 9.0V */

    /* =============================================
     * Phase 1: 启动充放电循环
     * ============================================= */
    printf("\n--- Phase 1: 启动充放电循环 ---\n");
    ret = learn_start_cycle(&session, demo_vreg, demo_ichg, demo_vcut);
    if (ret != BQ25710_OK) {
        printf("循环启动失败\n");
        return;
    }

    /*
     * CC 充电模拟 (实际项目中由 while 循环 + 定时器驱动):
     *
     * while (session.stage == LEARN_STAGE_CC_CHARGE) {
     *     // 读取 VBAT
     *     if (vbat >= vreg * 0.98f) { // 进入 CV
     *         session.stage = LEARN_STAGE_CV_CHARGE;
     *     }
     *     learn_coulomb_count(&session, 100); // 每100ms积分
     *     HAL_Delay(100);
     * }
     */
    printf("CC 充电: %u mA, 目标 %u mV\n", demo_ichg, demo_vreg);

    /* =============================================
     * Phase 2: 进入学习模式放电
     * ============================================= */
    printf("\n--- Phase 2: 进入学习模式放电 ---\n");

    /* 先停止充电 */
    bq25710_charging_enable(0);

    /* 进入学习模式 (IADPT 40x 增益, 精确电流监测) */
    ret = learn_enter(LEARN_IADPT_GAIN_40X);
    if (ret != BQ25710_OK) {
        printf("学习模式进入失败\n");
        return;
    }

    session.stage = LEARN_STAGE_DISCHARGE;

    printf("学习模式已激活: 电池开始放电至 %u mV\n", demo_vcut);

    /*
     * 放电模拟 (实际项目中):
     *
     * while (session.stage == LEARN_STAGE_DISCHARGE) {
     *     // 读取 VBAT
     *     if (vbat <= vcut) {
     *         session.stage = LEARN_STAGE_DISCHARGE_DONE;
     *         break;
     *     }
     *     learn_coulomb_count(&session, 100);
     *     HAL_Delay(100);
     * }
     */

    /* =============================================
     * Phase 3: 库仑计数积分模拟
     * ============================================= */
    printf("\n--- Phase 3: 库仑计数积分 ---\n");

    /*
     * 模拟 2000mAh 电池, 2A 充电 1h + CV 30min
     * 然后 1A 放电 2h
     *
     * 充电容量计算:
     *   CC: 2A × 1h = 2000 mAh
     *   CV: 平均 0.5A × 0.5h = 250 mAh
     *   总计充电: 2250 mAh
     *
     * 放电容量:
     *   1A × 2h = 2000 mAh
     *
     * 效率:
     *   2000 / 2250 = 88.9%
     */
    printf("模拟充电: 2A×1h + CV 0.5A×0.5h = 2250mAh 输入\n");
    printf("模拟放电: 1A×2h = 2000mAh 输出\n");
    printf("估算效率: 2000/2250 = 88.9%%\n");

    /* 演示积分计算 (模拟1次100ms调用) */
    printf("\n  实际 ADC 读数示例:\n");

    BQ25710_ADCIBAT_t adc;
    int16_t val;
    ret = bq25710_read_word(BQ25710_REG_ADC_IBAT, &val);
    if (ret == BQ25710_OK) {
        adc.all = (uint16_t)val;
        printf("  ADC_ICHG  raw = %u → %.0f mA (16x 增益)\n",
               adc.maps.ADC_ICHG,
               (float)adc.maps.ADC_ICHG * ADC_ICHG_LSB_MA_16X);
        printf("  ADC_IDCHG raw = %u → %.0f mA (16x 增益)\n",
               adc.maps.ADC_IDCHG,
               (float)adc.maps.ADC_IDCHG * ADC_IDCHG_LSB_MA_16X);
    }

    /* =============================================
     * Phase 4: 效率计算与保存
     * ============================================= */
    printf("\n--- Phase 4: 效率计算与参数保存 ---\n");

    /* 填入模拟数据 */
    session.charge_capacity_mah    = 2250.0f;
    session.discharge_capacity_mah = 2000.0f;
    session.charge_energy_mwh      = 25000.0f;  /* ~25Wh */
    session.discharge_energy_mwh   = 22000.0f;  /* ~22Wh */

    learn_calc_efficiency(&session);
    learn_save_params(&session);

    /* =============================================
     * Phase 5: 退出学习模式
     * ============================================= */
    printf("\n--- Phase 5: 退出学习模式 ---\n");

    /* 先停止放电, 恢复充电 */
    ret = learn_exit();
    if (ret == BQ25710_OK) {
        bq25710_charging_enable(1);
        printf("已退出学习模式, 充电恢复\n");
    }

    printf("\n========== 学习模式演示完成 ==========\n");
    printf("学习到的电池容量: %.0f mAh\n", session.discharge_capacity_mah);
    printf("充电效率: %.1f %%\n", session.charging_efficiency * 100.0f);
}

/* ========================================================================
 * main() 骨架
 * ======================================================================== */

#ifdef EXAMPLE_LEARN_MAIN_ENABLED

#include "soft_i2c.h"
extern struct soft_i2c_bus_t g_i2c_bus;

int main(void)
{
    int8_t ret;

    printf("BQ25710 学习模式与容量估算示例\n");

    ret = bq25710_init(&g_i2c_bus);
    if (ret != BQ25710_OK) {
        printf("驱动初始化失败\n");
        return -1;
    }

    bq25710_set_watchdog(BQ25710_WDT_88S);
    learn_demo();

    while (1) {
        /* HAL_Delay(1000); */
    }

    return 0;
}

#endif /* EXAMPLE_LEARN_MAIN_ENABLED */
