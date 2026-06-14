/**
 * @file bq25710_example_comparator.c
 * @brief BQ25710 独立比较器应用 (CMPIN/CMPOUT) - 完整实现
 *
 * 实现独立比较器的初始化、NTC 过温保护、适配器 OVP 和中断处理。
 *
 * @note 基于已修复的 bq25710.h 驱动API
 */

#include "bq25710_example_comparator.h"
#include <string.h>
#include <stdio.h>

/* 内部状态 */
static cmp_config_t s_cmp_config;

/* ========================================================================
 * comparator_init - 配置独立比较器
 *
 * 寄存器操作:
 *   ChargeOption1 bit[7]   CMP_REF:  参考电压
 *   ChargeOption1 bit[6]   CMP_POL:  极性
 *   ChargeOption1 bit[5:4] CMP_DEG:  消抖
 *   ChargeOption1 bit[3]   FORCE_LATCHOFF: 强制关闭
 *   ProchotOption1 bit[6]  PP_COMP: PROCHOT 触发
 *
 * 组合参数:
 *   CMP_REF=0, CMP_POL=0, CMP_DEG=0b01 (1μs):
 *     当 V_CMPIN > 2.3V → CMPOUT=低
 *     响应时间 1μs
 * ======================================================================== */

int8_t comparator_init(const cmp_config_t *config)
{
    if (!config) return BQ25710_ERR_NULL_PTR;

    int8_t ret;

    /* 保存配置 */
    memcpy(&s_cmp_config, config, sizeof(cmp_config_t));

    /*
     * Step 1: 配置 ChargeOption1 比较器位
     *
     * ChargeOption1 寄存器 (0x30):
     *   不覆盖其他字段, 读-修改-写
     */
    {
        BQ25710_ChargeOption1_t opt1;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_CHARGE_OPTION_1, &val);
        if (ret != BQ25710_OK) return ret;
        opt1.all = (uint16_t)val;

        opt1.maps.CMP_REF  = config->ref;
        opt1.maps.CMP_POL  = config->pol;
        opt1.maps.CMP_DEG  = (uint16_t)config->deg;
        opt1.maps.FORCE_LATCHOFF = config->latch_power_path;

        ret = bq25710_write_word(BQ25710_REG_CHARGE_OPTION_1, (int16_t)opt1.all);
        if (ret != BQ25710_OK) return ret;

        printf("[CMP] ChargeOption1 比较器配置:\n");
        printf("  CMP_REF = %u (%s)\n", config->ref,
               config->ref == CMP_REF_2V3 ? "2.3V" : "1.2V");
        printf("  CMP_POL = %u (%s)\n", config->pol,
               config->pol == CMP_POL_INVERT ? "反相" : "同相");
        printf("  CMP_DEG = %u (", (unsigned)config->deg);
        switch (config->deg) {
            case CMP_DEG_OFF: printf("无消抖"); break;
            case CMP_DEG_1US: printf("1μs"); break;
            case CMP_DEG_2MS: printf("2ms"); break;
            case CMP_DEG_5S:  printf("5s"); break;
        }
        printf(")\n");
        printf("  FORCE_LATCHOFF = %u\n", config->latch_power_path);
    }

    /*
     * Step 2: 配置 PROCHOT 触发 (ProchotOption1)
     */
    {
        BQ25710_ProchotOption1_t pro1;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_PROCHOT_OPTION_1, &val);
        if (ret != BQ25710_OK) return ret;
        pro1.all = (uint16_t)val;

        pro1.maps.PP_COMP = config->trigger_prochot ? 1 : 0;

        ret = bq25710_write_word(BQ25710_REG_PROCHOT_OPTION_1, (int16_t)pro1.all);
        if (ret != BQ25710_OK) return ret;

        printf("  PP_COMP = %u → PROCHOT %s\n",
               config->trigger_prochot,
               config->trigger_prochot ? "可触发" : "不触发");
    }

    printf("[CMP] 比较器初始化完成\n");
    return BQ25710_OK;
}

/* ========================================================================
 * comparator_read_status - 读取 CMPOUT 状态
 *
 * 从 ProchotStatus bit[6] STAT_COMP 读取。
 * ======================================================================== */

int8_t comparator_read_status(uint8_t *triggered)
{
    if (!triggered) return BQ25710_ERR_NULL_PTR;

    int8_t ret;
    BQ25710_ProchotStatus_t stat;
    int16_t val;

    ret = bq25710_read_word(BQ25710_REG_PROCHOT_STATUS, &val);
    if (ret != BQ25710_OK) return ret;
    stat.all = (uint16_t)val;

    *triggered = stat.maps.STAT_COMP;

    return BQ25710_OK;
}

/* ========================================================================
 * comparator_read_cmpin_mv - 通过 ADC 读取 CMPIN 电压
 *
 * ADC_CMPIN: 8-bit, LSB=12mV (ADC_FULLSCALE=1, 全量程 3.06V)
 * 或 LSB=8mV (ADC_FULLSCALE=0, 全量程 2.04V)
 *
 * 必须先使能 EN_ADC_CMPIN 并触发转换。
 * ======================================================================== */

int8_t comparator_read_cmpin_mv(uint16_t *voltage_mv)
{
    if (!voltage_mv) return BQ25710_ERR_NULL_PTR;

    int8_t ret;
    BQ25710_ADCIINCMPIN_t adc;
    int16_t val;

    /* 读取 ADC CMPIN 通道 (寄存器 0x25 低字节) */
    ret = bq25710_read_word(BQ25710_REG_ADC_IIN_CMPIN, &val);
    if (ret != BQ25710_OK) return ret;
    adc.all = (uint16_t)val;

    /*
     * ADC CMPIN LSB = 12mV (默认 ADC_FULLSCALE=1)
     * 全量程 = 12mV × 256 = 3072mV ≈ 3.06V
     */
    *voltage_mv = (uint16_t)((uint16_t)adc.maps.ADC_CMPIN * 12);

    return BQ25710_OK;
}

/* ========================================================================
 * comparator_ntc_overtemp_protect - NTC 过温保护
 *
 * 电路:
 *   VREF (1.2V) → R_pullup (3.3kΩ) → CMPIN ←→ NTC (10kΩ @25°C) → GND
 *
 *   25°C:  V_cmpin = 1.2V × 10k/(3.3k+10k) = 0.90V
 *   60°C:  V_cmpin = 1.2V × 3.3k/(3.3k+3.3k) = 0.60V  (NTC≈3.3kΩ)
 *
 * 配置:
 *   CMP_REF=1.2V: 参考电压 1.2V
 *   CMP_POL=NONINV (同相): V_CMPIN < 1.2V → CMPOUT=L (触发)
 *
 * 动作:
 *   CMPOUT=L → PROCHOT (CPU 降频降温)
 *   过温持续 → FORCE_LATCHOFF (关闭功率路径)
 * ======================================================================== */

int8_t comparator_ntc_overtemp_protect(void)
{
    cmp_config_t config;

    memset(&config, 0, sizeof(config));

    config.ref              = CMP_REF_1V2;     /* 参考 1.2V */
    config.pol              = CMP_POL_NONINV;  /* 同相: VIN<VREF → OUT=L */
    config.deg              = CMP_DEG_2MS;     /* 2ms 消抖 */
    config.trigger_prochot  = 1;               /* 触发 PROCHOT */
    config.latch_power_path = 1;               /* 过温闭锁 */
    config.threshold_voltage = 1.2f;            /* 1.2V 阈值 */

    printf("\n[CMP-NTC] 电池过温保护配置:\n");
    printf("  电路: VREF(1.2V) → 3.3kΩ → CMPIN ←→ NTC(10k@25°C) → GND\n");
    printf("  参考: 1.2V\n");
    printf("  极性: 同相 (V_CMPIN < 1.2V → 触发)\n");
    printf("  消抖: 2ms\n");
    printf("  NTC 参数: R25=10kΩ, B=3435 (NCP15XH103)\n");
    printf("\n  温度对应电压 (近似):\n");
    printf("    25°C:  0.90V  (安全)\n");
    printf("    45°C:  0.76V  (安全)\n");
    printf("    60°C:  0.60V  (触发 PROCHOT)\n");
    printf("    75°C:  0.45V  (触发闭锁, 功率路径关闭)\n");

    return comparator_init(&config);
}

/* ========================================================================
 * comparator_adapter_ovp - 适配器过压检测
 *
 * 电路:
 *   VBUS → R1 (90kΩ) → CMPIN → R2 (10kΩ) → GND
 *
 * 正常 19V: V_cmpin = 19V × 10k/100k = 1.9V
 * 过压 23V: V_cmpin = 23V × 10k/100k = 2.3V
 *
 * 配置:
 *   CMP_REF=2.3V: 参考电压 2.3V
 *   CMP_POL=INVERT (反相): V_CMPIN > 2.3V → CMPOUT=L (触发)
 *
 * 动作:
 *   V_CMPIN > 2.3V → PROCHOT + FORCE_LATCHOFF
 *   (适配器过压时立即关闭功率路径, 保护后级电路)
 *
 * 注意: 芯片内置 ACOV 保护 VBUS > 26V 才动作,
 *       此应用提供更早的 23V 阈值保护。
 * ======================================================================== */

int8_t comparator_adapter_ovp(void)
{
    cmp_config_t config;

    memset(&config, 0, sizeof(config));

    config.ref              = CMP_REF_2V3;    /* 参考 2.3V */
    config.pol              = CMP_POL_INVERT; /* 反相: VIN>VREF → OUT=L */
    config.deg              = CMP_DEG_1US;    /* 1μs 消抖 (快速响应) */
    config.trigger_prochot  = 1;              /* 触发 PROCHOT */
    config.latch_power_path = 1;              /* 过压闭锁 */
    config.threshold_voltage = 2.3f;          /* 2.3V 阈值 */

    printf("\n[CMP-OVP] 适配器过压检测配置:\n");
    printf("  电路: VBUS → 90kΩ → CMPIN → 10kΩ → GND\n");
    printf("  参考: 2.3V\n");
    printf("  极性: 反相 (V_CMPIN > 2.3V → 触发)\n");
    printf("  消抖: 1μs (极速响应)\n");
    printf("\n  输入电压对应 CMPIN:\n");
    printf("    19.0V (正常): 1.90V  (安全)\n");
    printf("    20.0V (偏高): 2.00V  (安全)\n");
    printf("    23.0V (过压): 2.30V  (触发! 关闭功率路径)\n");
    printf("    26.0V (芯片级ACOV): 2.60V  (芯片内置保护也触发)\n");

    return comparator_init(&config);
}

/* ========================================================================
 * comparator_demo - 完整比较器演示
 * ======================================================================== */

void comparator_demo(void)
{
    int8_t ret;
    uint8_t triggered;
    uint16_t cmpin_mv;

    printf("\n========== 独立比较器应用演示 ==========\n");

    /* =============================================
     * Demo 1: NTC 过温保护
     * ============================================= */
    printf("\n--- Demo 1: NTC 电池过温保护 ---\n");
    ret = comparator_ntc_overtemp_protect();
    if (ret != BQ25710_OK) {
        printf("NTC 配置失败\n");
        return;
    }

    printf("\n模拟读取 CMPIN 电压和比较器状态:\n");

    /* 使能 ADC CMPIN 通道 */
    {
        BQ25710_ADCOption_t adc_opt;
        int16_t val;
        bq25710_read_word(BQ25710_REG_ADC_OPTION, &val);
        adc_opt.all = (uint16_t)val;
        adc_opt.maps.EN_ADC_CMPIN = 1;
        adc_opt.maps.ADC_CONV      = 1; /* 连续转换 */
        bq25710_write_word(BQ25710_REG_ADC_OPTION, (int16_t)adc_opt.all);
    }

    /* 读 CMPIN 电压 */
    ret = comparator_read_cmpin_mv(&cmpin_mv);
    if (ret == BQ25710_OK) {
        printf("  CMPIN 电压: %u mV (ADC 原始)\n", cmpin_mv);
        if (cmpin_mv < 1200) {
            printf("  V_CMPIN < 1.2V → 过温! CPU 降频\n");
        } else {
            printf("  V_CMPIN > 1.2V → 温度正常\n");
        }
    }

    /* 读比较器状态 */
    ret = comparator_read_status(&triggered);
    if (ret == BQ25710_OK) {
        printf("  PROCHOT STAT_COMP: %s\n", triggered ? "已触发" : "未触发");
    }

    /* =============================================
     * Demo 2: 适配器过压检测
     * ============================================= */
    printf("\n--- Demo 2: 适配器过压检测 ---\n");
    ret = comparator_adapter_ovp();
    if (ret != BQ25710_OK) {
        printf("OVP 配置失败\n");
        return;
    }

    /* 读 CMPIN 电压 (过压配置下) */
    ret = comparator_read_cmpin_mv(&cmpin_mv);
    if (ret == BQ25710_OK) {
        printf("  CMPIN 电压: %u mV\n", cmpin_mv);
        float vbus = (float)cmpin_mv * (OVP_R1_OHM + OVP_R2_OHM) / (float)OVP_R2_OHM;
        printf("  推算 VBUS: %.1f V\n", vbus / 1000.0f);
    }

    /* =============================================
     * Demo 3: 中断处理框架
     * ============================================= */
    printf("\n--- Demo 3: 比较器中断处理框架 ---\n");
    printf(
        "/* 典型 ISR 框架:\n"
        " * void EXTI_CMPOUT_IRQHandler(void) {\n"
        " *     uint8_t triggered;\n"
        " *     comparator_read_status(&triggered);\n"
        " *     if (triggered) {\n"
        " *         // 识别当前模式\n"
        " *         if (current_mode == MODE_NTC_PROTECT) {\n"
        " *             // NTC 过温: 降充电电流, 通知 OS 降频\n"
        " *             bq25710_set_charge_current(new_lower_current);\n"
        " *             notify_os_thermal_throttle();\n"
        " *         } else if (current_mode == MODE_ADAPTER_OVP) {\n"
        " *             // 适配器过压: 立即关闭, 保存数据\n"
        " *             save_critical_data();\n"
        " *             system_shutdown();\n"
        " *         }\n"
        " *     }\n"
        " *     EXTI_ClearITPendingBit(EXTI_CMPOUT_PIN);\n"
        " * }\n"
        " */\n"
    );

    printf("\n========== 比较器演示完成 ==========\n");
}

/* ========================================================================
 * main() 骨架
 * ======================================================================== */

#ifdef EXAMPLE_COMPARATOR_MAIN_ENABLED

#include "soft_i2c.h"
extern struct soft_i2c_bus_t g_i2c_bus;

int main(void)
{
    int8_t ret;

    printf("BQ25710 独立比较器示例\n");

    ret = bq25710_init(&g_i2c_bus);
    if (ret != BQ25710_OK) {
        printf("驱动初始化失败\n");
        return -1;
    }

    comparator_demo();

    while (1) {
        /* 轮询比较器状态 */
        uint8_t triggered;
        comparator_read_status(&triggered);
        if (triggered) {
            printf("比较器触发! 执行保护动作\n");
        }
        /* HAL_Delay(100); */
    }

    return 0;
}

#endif /* EXAMPLE_COMPARATOR_MAIN_ENABLED */
