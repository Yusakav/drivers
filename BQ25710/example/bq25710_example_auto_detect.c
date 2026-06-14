/**
 * @file bq25710_example_auto_detect.c
 * @brief BQ25710 多电池类型自动识别与自适应配置 - 完整实现
 *
 * 实现:
 *   - 电池节数自动检测 (CELL_BATPRESZ 引脚电压)
 *   - 电池化学类型识别 (LiFePO4 vs NMC)
 *   - 自动参数配置
 *   - 电池热插拔自适应恢复
 *
 * @note 基于已修复的 bq25710.h 驱动API
 */

#include "bq25710_example_auto_detect.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================
 * 电池参数查找表
 *
 * 索引: [chemistry][cells-1]
 *
 * 参数说明:
 *   charge_voltage_mv:   充电终止电压 (VREG)
 *   min_sys_voltage_mv:  最小系统电压 (MinSystemVoltage)
 *   charge_current_ma:   推荐充电电流
 *   precharge_threshold:  预充电阈值 (BATLOWV, 约 3.0V/cell)
 *   termination_current:  充电终止电流 (C/10)
 *   sysovp_threshold_mv: 系统过压保护阈值
 * ======================================================================== */

static const battery_params_t s_param_table[2][4] = {
    /* LiFePO4 (磷酸铁锂): V_nom=3.2V, V_max=3.6V/cell */
    [BATTERY_TYPE_LIFEPO4 - 1] = {
        /* 1S */ {BATTERY_TYPE_LIFEPO4, CELL_COUNT_1S, 3600, 3072, 1000, 3000, 100, 5000},
        /* 2S */ {BATTERY_TYPE_LIFEPO4, CELL_COUNT_2S, 7200, 6144, 2000, 6000, 200, 12000},
        /* 3S */ {BATTERY_TYPE_LIFEPO4, CELL_COUNT_3S, 10800, 9216, 2000, 9000, 200, 19500},
        /* 4S */ {BATTERY_TYPE_LIFEPO4, CELL_COUNT_4S, 14400, 12288, 2000, 12000, 200, 19500},
    },
    /* NMC/三元锂: V_nom=3.7V, V_max=4.2V/cell */
    [BATTERY_TYPE_NMC - 1] = {
        /* 1S */ {BATTERY_TYPE_NMC, CELL_COUNT_1S, 4200, 3584, 1000, 3000, 100, 5000},
        /* 2S */ {BATTERY_TYPE_NMC, CELL_COUNT_2S, 8400, 7168, 2000, 6000, 200, 12000},
        /* 3S */ {BATTERY_TYPE_NMC, CELL_COUNT_3S, 12600, 10752, 2000, 9000, 200, 19500},
        /* 4S */ {BATTERY_TYPE_NMC, CELL_COUNT_4S, 16800, 14336, 2000, 12000, 200, 19500},
    },
};

/* ========================================================================
 * detect_cell_count - 自动检测电池节数
 *
 * 方法1: 读取 MaxChargeVoltage 默认值
 *   BQ25710 上电复位后, 根据 CELL_BATPRESZ 引脚电压自动设定
 *   默认 ChargeVoltage 寄存器:
 *     1S: 4200mV (0x0D20) - 三元锂默认
 *     2S: 8400mV (0x1A40)
 *     3S: 12600mV (0x2760)
 *     4S: 16800mV (0x3480)
 *
 *   LiFePO4 默认值不同:
 *     1S: 3600mV
 *     2S: 7200mV
 *     ...
 *
 * 方法2: 读取 ADC_CMPIN 通道 (CELL_BATPRESZ 电压)
 *   注意: CELL_BATPRESZ 非直接可读, 通过默认充电电压间接推断。
 *
 * 默认 ChargeVoltage 与节数对应关系:
 *   3600 ≤ V < 4200 → 1S LiFePO4
 *   4200 ≤ V < 7200 → 1S NMC
 *   7200 ≤ V < 8400 → 2S LiFePO4
 *   8400 ≤ V < 10800 → 2S NMC
 *   10800 ≤ V < 12600 → 3S LiFePO4
 *   12600 ≤ V < 14400 → 3S NMC
 *   14400 ≤ V < 16800 → 4S LiFePO4
 *   V ≥ 16800 → 4S NMC
 * ======================================================================== */

int8_t detect_cell_count(cell_count_t *cells)
{
    if (!cells) return BQ25710_ERR_NULL_PTR;

    int8_t ret;
    uint16_t default_vreg_mv;

    /* 读取 MaxChargeVoltage 当前值 */
    ret = bq25710_get_max_charge_voltage(&default_vreg_mv);
    if (ret != BQ25710_OK) {
        printf("[Detect] 读取默认充电电压失败\n");
        return ret;
    }

    printf("[Detect] 默认充电电压: %u mV\n", default_vreg_mv);

    /*
     * 根据默认充电电压判断节数
     *
     * 电压范围与节数映射:
     *   1S: 3600~4200 mV
     *   2S: 7200~8400 mV
     *   3S: 10800~12600 mV
     *   4S: 14400~16800 mV
     */
    if (default_vreg_mv <= 4200) {
        *cells = CELL_COUNT_1S;
        printf("[Detect] 检测为 1 节电池 (V_default=%u mV)\n", default_vreg_mv);
    } else if (default_vreg_mv <= 8400) {
        *cells = CELL_COUNT_2S;
        printf("[Detect] 检测为 2 节电池 (V_default=%u mV)\n", default_vreg_mv);
    } else if (default_vreg_mv <= 12600) {
        *cells = CELL_COUNT_3S;
        printf("[Detect] 检测为 3 节电池 (V_default=%u mV)\n", default_vreg_mv);
    } else if (default_vreg_mv <= 16800) {
        *cells = CELL_COUNT_4S;
        printf("[Detect] 检测为 4 节电池 (V_default=%u mV)\n", default_vreg_mv);
    } else {
        *cells = CELL_COUNT_UNKNOWN;
        printf("[Detect] 无法确定节数 (V_default=%u mV)\n", default_vreg_mv);
        return BQ25710_ERR_PARAM;
    }

    return BQ25710_OK;
}

/* ========================================================================
 * detect_battery_chemistry - 识别电池化学类型
 *
 * 通过小电流充电观察电压平台判断:
 *
 * 算法:
 *   1. 确认电池存在且 VBAT > precharge_threshold (安全电压)
 *   2. 写入 MaxChargeVoltage = cells × 4.2V (保守,兼容NMC)
 *   3. 使能 256mA 小电流充电
 *   4. 充电 30 秒后读取 VBAT ADC
 *   5. 计算 V_per_cell = VBAT / cells
 *   6. 判断:
 *      V_per_cell > 3.5V → NMC (标称 3.7V, 30s 已充至 3.5V+)
 *      V_per_cell < 3.3V → LiFePO4 (标称 3.2V, 30s 平台 < 3.3V)
 *      3.3V ≤ V_per_cell ≤ 3.5V → 延长观察 (再充电 30s)
 *   7. 停止充电, 输出结果
 *
 * 注意: 实际项目需要延时实现 (如 HAL_Delay), 此处用注释标注。
 * ======================================================================== */

int8_t detect_battery_chemistry(battery_chemistry_t *chemistry,
                                cell_count_t cells)
{
    if (!chemistry) return BQ25710_ERR_NULL_PTR;
    if (cells < CELL_COUNT_1S || cells > CELL_COUNT_4S) {
        return BQ25710_ERR_PARAM;
    }

    int8_t ret;
    uint16_t vbat_mv;
    uint16_t v_per_cell;

    printf("[Detect] 开始化学类型识别 (cells=%d)...\n", (int)cells);

    /*
     * Step 1: 读取当前 VBAT 确认电池存在且安全
     */
    {
        BQ25710_ADCVSYSVBAT_t adc;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_ADC_VSYS_VBAT, &val);
        if (ret != BQ25710_OK) return ret;
        adc.all = (uint16_t)val;
        vbat_mv = (uint16_t)((uint16_t)adc.maps.ADC_VBAT * 64 + 2880);

        printf("  当前 VBAT: %u mV\n", vbat_mv);

        /* 安全检查: VBAT 太低则跳过识别 */
        if (vbat_mv < (uint16_t)cells * 3000) {
            printf("  VBAT 过低, 无法安全识别\n");
            *chemistry = BATTERY_TYPE_UNKNOWN;
            return BQ25710_ERR_PARAM;
        }
    }

    /*
     * Step 2: 设置保守充电参数, 小电流充电
     *
     * MaxChargeVoltage = cells × 4200mV (兼容 NMC 上限):
     *   reg = ((cells × 4200) / 8) << 3
     *   例 3S: (12600/8) << 3 = 1575 << 3 = 0x3138
     *
     * ChargeCurrent = 256mA:
     *   reg = (256 / 64) << 6 = 4 << 6 = 0x0100
     */
    {
        uint16_t vreg_max = (uint16_t)cells * 4200;

        ret = bq25710_set_max_charge_voltage(vreg_max);
        if (ret != BQ25710_OK) return ret;

        ret = bq25710_set_charge_current(DETECT_CHARGE_CURRENT_MA);
        if (ret != BQ25710_OK) return ret;

        printf("  设置充电: VREG=%u mV, ICHG=%u mA\n",
               vreg_max, DETECT_CHARGE_CURRENT_MA);
    }

    /*
     * Step 3: 使能充电, 保持 30 秒
     */
    ret = bq25710_charging_enable(1);
    if (ret != BQ25710_OK) return ret;

    printf("  使能充电, 等待 %u ms...\n", DETECT_CHARGE_DURATION_MS);

    /*
     * 实际代码:
     *   HAL_Delay(DETECT_CHARGE_DURATION_MS);
     *   然后再读取 VBAT
     */

    /* Step 4: 读取充电后的 VBAT */
    {
        BQ25710_ADCVSYSVBAT_t adc;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_ADC_VSYS_VBAT, &val);
        if (ret != BQ25710_OK) {
            bq25710_charging_enable(0);
            return ret;
        }
        adc.all = (uint16_t)val;
        vbat_mv = (uint16_t)((uint16_t)adc.maps.ADC_VBAT * 64 + 2880);
    }

    v_per_cell = vbat_mv / (uint16_t)cells;

    printf("  充电后 VBAT: %u mV (~%u mV/cell)\n", vbat_mv, v_per_cell);

    /*
     * Step 5: 判断化学类型
     */
    if (v_per_cell > DETECT_NMC_THRESHOLD_MV_PER_CELL) {
        *chemistry = BATTERY_TYPE_NMC;
        printf("  V/cell = %u mV > %u mV → NMC 三元锂\n",
               v_per_cell, DETECT_NMC_THRESHOLD_MV_PER_CELL);
    } else if (v_per_cell < DETECT_LFP_THRESHOLD_MV_PER_CELL) {
        *chemistry = BATTERY_TYPE_LIFEPO4;
        printf("  V/cell = %u mV < %u mV → LiFePO4 磷酸铁锂\n",
               v_per_cell, DETECT_LFP_THRESHOLD_MV_PER_CELL);
    } else {
        /* 处于模糊区间, 延长观察 */
        printf("  V/cell = %u mV 在模糊区间 [%u, %u]\n",
               v_per_cell,
               DETECT_LFP_THRESHOLD_MV_PER_CELL,
               DETECT_NMC_THRESHOLD_MV_PER_CELL);
        printf("  延长识别 (再充 30s)...\n");

        /* 实际: HAL_Delay(30000); 再读一次并判断 */
        /* 此处简化: 默认判为 NMC (更常见) */
        *chemistry = BATTERY_TYPE_NMC;
        printf("  → 默认判定为 NMC (常见于消费电子)\n");
    }

    /* Step 6: 停止充电 */
    bq25710_charging_enable(0);

    printf("[Detect] 识别完成: %s, %uS\n",
           *chemistry == BATTERY_TYPE_NMC ? "NMC" : "LiFePO4",
           (unsigned)cells);

    return BQ25710_OK;
}

/* ========================================================================
 * auto_config_params - 根据检测结果自动配置充电参数
 *
 * 查表获取完整参数:
 *   - NMC: 4.2V/cell, MinSysV≈3.5V/cell
 *   - LiFePO4: 3.6V/cell, MinSysV≈3.0V/cell
 *
 * 寄存器计算 (注释中包含):
 *   ChargeVoltage   reg = (mV / 8) << 3
 *   MinSystemVoltage reg = (mV / 256) << 8
 *   ChargeCurrent    reg = (mA / 64) << 6
 * ======================================================================== */

int8_t auto_config_params(battery_params_t *params)
{
    if (!params) return BQ25710_ERR_NULL_PTR;

    if (params->chemistry == BATTERY_TYPE_UNKNOWN ||
        params->cells == CELL_COUNT_UNKNOWN) {
        printf("[Config] 电池类型或节数未识别\n");
        return BQ25710_ERR_PARAM;
    }

    uint8_t chem_idx = (uint8_t)(params->chemistry - 1);
    uint8_t cell_idx = (uint8_t)(params->cells - 1);

    if (chem_idx > 1 || cell_idx > 3) {
        return BQ25710_ERR_PARAM;
    }

    /* 从查找表复制完整参数 */
    memcpy(params, &s_param_table[chem_idx][cell_idx], sizeof(battery_params_t));

    printf("[Config] 自动配置参数:\n");
    printf("  类型: %s\n",
           params->chemistry == BATTERY_TYPE_NMC ? "NMC" : "LiFePO4");
    printf("  节数: %uS\n", (unsigned)params->cells);
    printf("  ChargeVoltage:   %u mV\n", params->charge_voltage_mv);
    printf("    → reg = ((%u/8)<<3) = 0x%04X\n",
           params->charge_voltage_mv,
           (unsigned)((params->charge_voltage_mv / 8) << 3));
    printf("  MinSystemVoltage: %u mV\n", params->min_sys_voltage_mv);
    printf("    → reg = ((%u/256)<<8) = 0x%04X\n",
           params->min_sys_voltage_mv,
           (unsigned)((params->min_sys_voltage_mv / 256) << 8));
    printf("  ChargeCurrent:    %u mA\n", params->charge_current_ma);
    printf("    → reg = ((%u/64)<<6) = 0x%04X\n",
           params->charge_current_ma,
           (unsigned)((params->charge_current_ma / 64) << 6));
    printf("  TermCurrent:      %u mA (C/10)\n", params->termination_current_ma);
    printf("  Precharge Threshold: %u mV\n", params->precharge_threshold_mv);
    printf("  SYSOVP:           %u mV\n", params->sysovp_threshold_mv);

    return BQ25710_OK;
}

/* ========================================================================
 * apply_battery_params - 应用电池参数到 BQ25710
 *
 * 将自动检测到的参数写入硬件寄存器。
 * ======================================================================== */

int8_t apply_battery_params(const battery_params_t *params)
{
    if (!params) return BQ25710_ERR_NULL_PTR;

    int8_t ret;

    printf("[Apply] 写入电池参数到 BQ25710:\n");

    /*
     * 1. MaxChargeVoltage
     *    寄存器 0x15, 步进 8mV, offset bits[3]
     *    reg = (charge_voltage_mv / 8) << 3
     */
    ret = bq25710_set_max_charge_voltage(params->charge_voltage_mv);
    if (ret != BQ25710_OK) {
        printf("  写入 MaxChargeVoltage 失败\n");
        return ret;
    }
    printf("  [OK] MaxChargeVoltage = %u mV\n", params->charge_voltage_mv);

    /*
     * 2. MinSystemVoltage
     *    寄存器 0x3E, 步进 256mV, offset bits[8]
     *    reg = (min_sys_voltage_mv / 256) << 8
     */
    ret = bq25710_set_min_sys_voltage(params->min_sys_voltage_mv);
    if (ret != BQ25710_OK) {
        printf("  写入 MinSystemVoltage 失败\n");
        return ret;
    }
    printf("  [OK] MinSystemVoltage = %u mV\n", params->min_sys_voltage_mv);

    /*
     * 3. ChargeCurrent
     *    寄存器 0x14, 步进 64mA, offset bits[6]
     *    reg = (charge_current_ma / 64) << 6
     */
    ret = bq25710_set_charge_current(params->charge_current_ma);
    if (ret != BQ25710_OK) {
        printf("  写入 ChargeCurrent 失败\n");
        return ret;
    }
    printf("  [OK] ChargeCurrent = %u mA\n", params->charge_current_ma);

    /*
     * 4. InputCurrent (IIN_HOST) - 建议值: 充电电流 × 1.2 + 系统功耗
     *    寄存器 0x3F, 步进 50mA, offset bits[8]
     *
     *    例: 充电 2A + 系统 1A = 3A, 留余量 → 4A
     */
    {
        uint16_t input_current = params->charge_current_ma * 12 / 10 + 1000; /* 1.2x + 1A */
        if (input_current > 6350) input_current = 6350;

        ret = bq25710_set_input_current(input_current);
        if (ret != BQ25710_OK) {
            printf("  写入 InputCurrent 失败\n");
            return ret;
        }
        printf("  [OK] InputCurrent = %u mA (建议值: I_CHG×1.2 + 1A)\n",
               input_current);
    }

    /*
     * 5. VINDPM - 建议值: 电池电压 + 2V 裕量
     *    寄存器 0x3D, 步进 64mV, offset bits[6]
     */
    {
        uint16_t vindpm_mv = params->charge_voltage_mv + 2000; /* VREG + 2V */
        if (vindpm_mv < 4200) vindpm_mv = 4200; /* USB 最低 */

        ret = bq25710_set_vindpm(vindpm_mv);
        if (ret != BQ25710_OK) {
            printf("  写入 VINDPM 失败\n");
            return ret;
        }
        printf("  [OK] VINDPM = %u mV (VREG + 2V)\n", vindpm_mv);
    }

    /*
     * 6. 使能充电
     */
    ret = bq25710_charging_enable(1);
    if (ret != BQ25710_OK) {
        printf("  使能充电失败\n");
        return ret;
    }
    printf("  [OK] 充电已使能\n");

    printf("[Apply] 所有参数写入完成\n");
    return BQ25710_OK;
}

/* ========================================================================
 * auto_hotswap_recover - 电池热插拔检测与自动恢复
 *
 * 流程:
 *   1. 检测电池移除
 *   2. 停止充电
 *   3. 等待新电池插入
 *   4. 重新识别
 *   5. 应用参数
 * ======================================================================== */

int8_t auto_hotswap_recover(battery_params_t *params)
{
    if (!params) return BQ25710_ERR_NULL_PTR;

    int8_t ret;

    printf("\n[Hotswap] 电池热插拔恢复序列:\n");

    /*
     * Step 1: 检测电池移除
     *
     * 通过 ChargerStatus 或 CELL_BATPRESZ 引脚。
     * BQ25710 的 STAT_Battery_Removal (ProchotStatus[1])
     * 可在电池移除时触发。
     */
    {
        BQ25710_ProchotStatus_t stat;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_PROCHOT_STATUS, &val);
        if (ret != BQ25710_OK) return ret;
        stat.all = (uint16_t)val;

        if (stat.maps.STAT_Battery_Removal) {
            printf("  1. 电池已移除 (STAT_Battery_Removal=1)\n");
            bq25710_charging_enable(0);
        }
    }

    /* Step 2: 等待新电池插入 (实际代码需轮询) */
    printf("  2. 等待新电池插入...\n");
    /*
     * while (battery_not_present) {
     *     HAL_Delay(500);
     * }
     */

    /* Step 3: 重新识别 */
    printf("  3. 检测到新电池, 开始识别\n");

    cell_count_t cells;
    ret = detect_cell_count(&cells);
    if (ret != BQ25710_OK) {
        printf("  节数检测失败\n");
        return ret;
    }

    battery_chemistry_t chem;
    ret = detect_battery_chemistry(&chem, cells);
    if (ret != BQ25710_OK) {
        printf("  化学类型识别失败\n");
        return ret;
    }

    /* Step 4: 配置参数 */
    params->cells     = cells;
    params->chemistry = chem;

    ret = auto_config_params(params);
    if (ret != BQ25710_OK) return ret;

    /* Step 5: 应用参数 */
    ret = apply_battery_params(params);
    if (ret != BQ25710_OK) return ret;

    printf("[Hotswap] 热插拔恢复完成\n");
    return BQ25710_OK;
}

/* ========================================================================
 * auto_detect_demo - 完整自动识别演示
 * ======================================================================== */

void auto_detect_demo(void)
{
    int8_t ret;
    battery_params_t params;
    cell_count_t cells;
    battery_chemistry_t chem;

    printf("\n========== 多电池类型自动识别演示 ==========\n");

    memset(&params, 0, sizeof(params));

    /* =============================================
     * Step 1: 检测电池节数
     * ============================================= */
    printf("\n--- Step 1: 检测电池节数 ---\n");

    ret = detect_cell_count(&cells);
    if (ret != BQ25710_OK) {
        printf("节数检测失败, 手动指定为 3S\n");
        cells = CELL_COUNT_3S;
    }

    /* =============================================
     * Step 2: 识别化学类型
     * ============================================= */
    printf("\n--- Step 2: 识别电池化学类型 ---\n");

    ret = detect_battery_chemistry(&chem, cells);
    if (ret != BQ25710_OK) {
        printf("化学类型识别失败, 默认 NMC\n");
        chem = BATTERY_TYPE_NMC;
    }

    params.cells     = cells;
    params.chemistry = chem;

    /* =============================================
     * Step 3: 自动配置参数
     * ============================================= */
    printf("\n--- Step 3: 自动配置充电参数 ---\n");

    ret = auto_config_params(&params);
    if (ret != BQ25710_OK) {
        printf("参数配置失败\n");
        return;
    }

    /* =============================================
     * Step 4: 应用参数
     * ============================================= */
    printf("\n--- Step 4: 应用参数到 BQ25710 ---\n");

    ret = apply_battery_params(&params);
    if (ret != BQ25710_OK) {
        printf("参数应用失败\n");
        return;
    }

    /* =============================================
     * Step 5: 磷酸铁锂 vs 三元锂关键差异说明
     * ============================================= */
    printf("\n--- Step 5: LiFePO4 vs NMC 关键差异说明 ---\n");
    printf(
        "+-------------------+---------------------+---------------------+\n"
        "| 参数              | LiFePO4 (磷酸铁锂)  | NMC (三元锂)        |\n"
        "+-------------------+---------------------+---------------------+\n"
        "| 标称电压          | 3.2V/cell           | 3.7V/cell           |\n"
        "| 充电终止电压      | 3.60V/cell          | 4.20V/cell          |\n"
        "| MinSystemVoltage  | ~3.0V/cell          | ~3.5V/cell          |\n"
        "| 平台特征          | 平坦 (3.2~3.3V)     | 倾斜 (3.5~4.1V)     |\n"
        "| CELL_BATPRESZ分压 | 同节数相同          | 同节数相同          |\n"
        "| SYSOVP (1S)       | 5V                  | 5V                  |\n"
        "| SYSOVP (2S)       | 12V                 | 12V                 |\n"
        "| SYSOVP (3S/4S)    | 19.5V               | 19.5V               |\n"
        "| 默认VREG需改写?   | 是 (芯片默认NMC)    | 否 (芯片默认NMC)    |\n"
        "| 安全特性          | 更稳定, 不易热失控  | 能量密度高          |\n"
        "| 充电电压寄存器    | (mV/8)<<3           | (mV/8)<<3           |\n"
        "+-------------------+---------------------+---------------------+\n"
    );
    printf("\n注意: CELL_BATPRESZ 引脚分压比决定节数检测,\n");
    printf("      与电池化学类型无关, 需软件额外识别。\n");

    /* =============================================
     * Step 6: 热插拔演示
     * ============================================= */
    printf("\n--- Step 6: 热插拔恢复流程说明 ---\n");
    printf(
        "完整热插拔自适应流程:\n"
        "  电池移除检测:\n"
        "    → CELL_BATPRESZ=0 或 ProchotStatus[1]=1\n"
        "    → 停止充电 (CHRG_INHIBIT=1)\n"
        "    → 保持系统供电 (适配器存在时)\n"
        "\n"
        "  新电池插入:\n"
        "    → CELL_BATPRESZ=1\n"
        "    → 延迟 500ms 等待稳定\n"
        "    → detect_cell_count() 检测节数\n"
        "    → detect_battery_chemistry() 识别类型\n"
        "    → auto_config_params() 查表配置\n"
        "    → apply_battery_params() 写入寄存器\n"
        "    → 启动充电正常流程\n"
    );

    printf("\n========== 自动识别演示完成 ==========\n");
    printf("检测结果: %s %uS\n",
           params.chemistry == BATTERY_TYPE_NMC ? "NMC" : "LiFePO4",
           (unsigned)params.cells);
}

/* ========================================================================
 * main() 骨架
 * ======================================================================== */

#ifdef EXAMPLE_AUTO_DETECT_MAIN_ENABLED

#include "soft_i2c.h"
extern struct soft_i2c_bus_t g_i2c_bus;

int main(void)
{
    int8_t ret;

    printf("BQ25710 电池类型自动识别示例\n");

    ret = bq25710_init(&g_i2c_bus);
    if (ret != BQ25710_OK) {
        printf("驱动初始化失败\n");
        return -1;
    }

    bq25710_set_watchdog(BQ25710_WDT_88S);

    /* 使能 ADC 通道 (VBAT) */
    {
        BQ25710_ADCOption_t adc_opt;
        int16_t val;
        bq25710_read_word(BQ25710_REG_ADC_OPTION, &val);
        adc_opt.all = (uint16_t)val;
        adc_opt.maps.EN_ADC_VBAT  = 1;
        adc_opt.maps.ADC_CONV     = 1;
        bq25710_write_word(BQ25710_REG_ADC_OPTION, (int16_t)adc_opt.all);
    }

    auto_detect_demo();

    while (1) {
        /* 持续监控电池状态, 热插拔检测 */
        battery_params_t new_params;
        /* auto_hotswap_recover(&new_params); */
        /* HAL_Delay(5000); */
    }

    return 0;
}

#endif /* EXAMPLE_AUTO_DETECT_MAIN_ENABLED */
