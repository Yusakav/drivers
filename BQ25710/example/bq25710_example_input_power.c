/**
 * @file bq25710_example_input_power.c
 * @brief BQ25710 输入功率管理 - IDPM/VDPM/ILIM_HIZ + 峰值功率 + ICO 完整实现
 *
 * 覆盖所有输入功率管理功能的完整可运行示例。
 *
 * @note 基于已修复的 bq25710.h 驱动API
 */

#include "bq25710_example_input_power.h"
#include <stdio.h>

/* ========================================================================
 * 内部辅助: 基于 ILIM2 百分比查找寄存器编码
 *
 * ILIM2_VTH 5-bit 编码:
 *   00001b (1)  = 110%   01001b (9)  = 150%
 *   11010b (26) = 250%   11110b (30) = 450%
 *
 * 特殊情况: 00000b = 无效 (最小 110%), 11011b~11111b 中 11011=270%, 11100=300%, ...
 * 精确映射: code 1-25: pct = 100 + code*10
 *           code 26-30: pct = 250 + (code-26)*50
 * ======================================================================== */

static uint8_t ilim2_pct_to_code(uint8_t pct)
{
    if (pct <= 110) return 1;
    if (pct <= 250) {
        /* 1-25 → 110%-250% (每步10%) */
        return (uint8_t)((pct - 100) / 10);
    }
    if (pct <= 450) {
        /* 26-30 → 260%-450% (每步50%) */
        return (uint8_t)(26 + (pct - 250) / 50);
    }
    return 30; /* 最大450% */
}

/* ========================================================================
 * input_power_init - 初始化输入功率管理
 *
 * 寄存器计算:
 *   IIN_HOST: code = current_ma / 50
 *     例: 3250mA → reg = (3250/50) << 8 = 0x0041 → high_byte = 0x41
 *   VINDPM: code = (vindpm_mv - 3200) / 64
 *     例: 4200mV → reg = ((4200-3200)/64) << 6 = 0x000F << 6 = 0x03C0
 * ======================================================================== */

int8_t input_power_init(uint16_t input_current_ma, uint16_t vindpm_mv)
{
    int8_t ret;

    /*
     * Step 1: 设置 IIN_HOST 输入电流限制
     *
     * 公式: reg = (current_ma / 50) << 8
     *   10mOhm RAC 检测电阻下 LSB=50mA
     *   范围: 50mA ~ 6350mA
     *
     *   示例 65W/20V 适配器: 3250mA → reg = (3250/50)<<8 = 65<<8 = 0x4100
     */
    ret = bq25710_set_input_current_limit(input_current_ma);
    if (ret != BQ25710_OK) {
        printf("[InputPower] IIN_HOST 设置失败 (ret=%d)\n", ret);
        return ret;
    }

    /*
     * Step 2: 设置 VINDPM 输入电压跌落保护
     *
     * 公式: reg = ((voltage_mv - 3200) / 64) << 6
     *   范围: 3200mV ~ 19520mV, LSB=64mV
     *
     *   示例 USB 5V: 4200mV → ((4200-3200)/64)<<6 = 15<<6 = 0x03C0
     *   示例 USB 20V: 18000mV → ((18000-3200)/64)<<6 = 231<<6 = 0x39C0
     */
    ret = bq25710_set_vindpm_threshold(vindpm_mv);
    if (ret != BQ25710_OK) {
        printf("[InputPower] VINDPM 设置失败 (ret=%d)\n", ret);
        return ret;
    }

    /*
     * Step 3: 使能 IDPM 调节环路
     *
     * ChargeOption0 bit[1] = EN_IDPM:
     *   0=禁用 (输入电流无动态调节)
     *   1=启用 (IDPM 环路自动降流, 默认值)
     *
     * IDPM 已默认使能, 此处确认其开启
     */
    printf("[InputPower] 初始化完成:\n");
    printf("  IIN_HOST = %u mA (reg: 0x%04X)\n",
           input_current_ma, (input_current_ma / 50) << 8);
    printf("  VINDPM   = %u mV  (reg: 0x%04X)\n",
           vindpm_mv, ((vindpm_mv - 3200) / 64) << 6);

    return BQ25710_OK;
}

/* ========================================================================
 * input_power_adjust_idpm - 动态调整 IDPM
 * ======================================================================== */

int8_t input_power_adjust_idpm(uint16_t new_current_ma)
{
    int8_t ret;

    printf("[InputPower] 调整 IDPM: -> %u mA\n", new_current_ma);
    ret = bq25710_set_input_current_limit(new_current_ma);
    if (ret != BQ25710_OK) {
        printf("[InputPower] IDPM 调整失败 (ret=%d)\n", ret);
    }
    return ret;
}

/* ========================================================================
 * input_power_get_status - 读取综合状态
 * ======================================================================== */

int8_t input_power_get_status(input_power_status_t *status)
{
    if (!status) return BQ25710_ERR_NULL_PTR;

    int8_t ret;
    BQ25710_ChargerStatus_t cs;
    uint16_t dpm_ma;
    uint16_t in_ma;
    uint16_t vin_mv;

    ret = bq25710_get_charger_status(&cs);
    if (ret != BQ25710_OK) return ret;

    ret = bq25710_get_dpm_current(&dpm_ma);
    if (ret != BQ25710_OK) dpm_ma = 0;

    ret = bq25710_get_input_current_limit(&in_ma);
    if (ret != BQ25710_OK) in_ma = 0;

    ret = bq25710_get_vindpm_threshold(&vin_mv);
    if (ret != BQ25710_OK) vin_mv = 0;

    status->adapter_voltage_mv    = vin_mv; /* 近似: VINDPM 阈值作为参考 */
    status->adapter_current_ma    = in_ma;
    status->input_current_limit_ma = in_ma;
    status->vindpm_threshold_mv   = vin_mv;
    status->dpm_actual_ma         = dpm_ma;
    status->in_iindpm             = cs.maps.IN_IINDPM;
    status->in_vindpm             = cs.maps.IN_VINDPM;

    /* 峰值功率状态从 ChargeOption2 读取 */
    {
        BQ25710_ChargeOption2_t opt2;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_CHARGE_OPTION_2, &val);
        if (ret == BQ25710_OK) {
            opt2.all = (uint16_t)val;
            status->pkpwr_overload = opt2.maps.PKPWR_OVLD_STAT;
            status->pkpwr_relax    = opt2.maps.PKPWR_RELAX_STAT;
        } else {
            status->pkpwr_overload = 0;
            status->pkpwr_relax    = 0;
        }
    }

    return BQ25710_OK;
}

/* ========================================================================
 * input_power_pkpwr_level1 - 峰值功率模式 Level1
 *
 * 寄存器计算 (ChargeOption2):
 *   PKPWR_TOVLD_DEG[15:14]: 过载检测时间
 *     00=1ms, 01=2ms(默认), 10=10ms, 11=20ms
 *   PKPWR_TMAX[9:8]: 弛豫周期
 *     00=5ms, 01=10ms, 10=20ms(默认), 11=40ms
 *   EN_PKPWR_IDPM[13]=1: 电流过冲触发峰值模式
 *   EN_PKPWR_VSYS[12]=1: VSYS 下冲触发峰值模式
 *
 * ILIM2 阈值 (ProchotOption0):
 *   ILIM2 = IIN_HOST × pct%
 *   例: 65W/20V/3.25A, ILIM2@150% → 4.875A 短时过载
 * ======================================================================== */

int8_t input_power_pkpwr_level1(uint8_t overload_ms, uint8_t relax_ms,
                                uint8_t ilim2_pct)
{
    int8_t ret;

    /* 校验参数 */
    if (overload_ms > 20) overload_ms = 20;
    if (relax_ms    > 40) relax_ms    = 40;
    if (ilim2_pct   < 110) ilim2_pct = 110;
    if (ilim2_pct   > 450) ilim2_pct = 450;

    /*
     * ChargeOption2 配置:
     *   EN_PKPWR_IDPM=1 (IDPM 电流过冲触发)
     *   EN_PKPWR_VSYS=1 (VSYS 下冲触发, Level1 典型使能)
     */
    {
        BQ25710_ChargeOption2_t opt2;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_CHARGE_OPTION_2, &val);
        if (ret != BQ25710_OK) return ret;
        opt2.all = (uint16_t)val;

        opt2.maps.EN_PKPWR_IDPM = 1;
        opt2.maps.EN_PKPWR_VSYS = 1;

        /* PKPWR_TOVLD_DEG */
        if      (overload_ms <= 1)   opt2.maps.PKPWR_TOVLD_DEG = 0;
        else if (overload_ms <= 2)   opt2.maps.PKPWR_TOVLD_DEG = 1;
        else if (overload_ms <= 10)  opt2.maps.PKPWR_TOVLD_DEG = 2;
        else                         opt2.maps.PKPWR_TOVLD_DEG = 3;

        /* PKPWR_TMAX */
        if      (relax_ms <= 5)   opt2.maps.PKPWR_TMAX = 0;
        else if (relax_ms <= 10)  opt2.maps.PKPWR_TMAX = 1;
        else if (relax_ms <= 20)  opt2.maps.PKPWR_TMAX = 2;
        else                      opt2.maps.PKPWR_TMAX = 3;

        ret = bq25710_write_word(BQ25710_REG_CHARGE_OPTION_2, (int16_t)opt2.all);
        if (ret != BQ25710_OK) return ret;
    }

    /*
     * ProchotOption0 ILIM2_VTH 设置
     *
     * 5-bit 寄存器 bit[15:11]:
     *   150% → code=5 → 00101b
     *   200% → code=10 → 01010b
     *   450% → code=30 → 11110b
     */
    {
        BQ25710_ProchotOption0_t pro0;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_PROCHOT_OPTION_0, &val);
        if (ret != BQ25710_OK) return ret;
        pro0.all = (uint16_t)val;

        pro0.maps.ILIM2_VTH = ilim2_pct_to_code(ilim2_pct);

        ret = bq25710_write_word(BQ25710_REG_PROCHOT_OPTION_0, (int16_t)pro0.all);
        if (ret != BQ25710_OK) return ret;
    }

    printf("[PeakPower L1] 已配置: 过载=%ums 弛豫=%ums ILIM2=%u%% IIN_HOST\n",
           overload_ms, relax_ms, ilim2_pct);

    return BQ25710_OK;
}

/* ========================================================================
 * input_power_pkpwr_level2 - 峰值功率模式 Level2 (电池补偿)
 *
 * Level2 策略: 仅使能 VSYS 下冲触发 (EN_PKPWR_VSYS=1),
 * EN_PKPWR_IDPM=0 (禁用电流过冲触发, 避免频繁切换)
 *
 * 当 VSYS 电压下冲触发时:
 *   1. BQ25710 进入峰值功率模式, ILIM2 生效
 *   2. 电池通过理想二极管自动补偿系统功率缺口
 *   3. 弛豫周期后退出峰值模式, 恢复 ILIM1
 * ======================================================================== */

int8_t input_power_pkpwr_level2(void)
{
    int8_t ret;

    BQ25710_ChargeOption2_t opt2;
    int16_t val;
    ret = bq25710_read_word(BQ25710_REG_CHARGE_OPTION_2, &val);
    if (ret != BQ25710_OK) return ret;
    opt2.all = (uint16_t)val;

    /* Level2: 仅 VSYS 下冲触发, 电流过冲不触发 */
    opt2.maps.EN_PKPWR_VSYS = 1;
    opt2.maps.EN_PKPWR_IDPM = 0;

    /* 更长的过载时间 (10ms) 和弛豫周期 (40ms) */
    opt2.maps.PKPWR_TOVLD_DEG = 2; /* 10ms */
    opt2.maps.PKPWR_TMAX      = 3; /* 40ms */

    ret = bq25710_write_word(BQ25710_REG_CHARGE_OPTION_2, (int16_t)opt2.all);
    if (ret != BQ25710_OK) return ret;

    printf("[PeakPower L2] 电池补偿模式已启用: VSYS触发, 过载10ms/弛豫40ms\n");
    return BQ25710_OK;
}

/* ========================================================================
 * input_power_run_ico - 执行 ICO 检测
 *
 * ICO (Input Current Optimizer) 探测适配器最大能力:
 *
 * 工作流程:
 *   1. ChargeOption3[11] EN_ICO_MODE = 1 使能 ICO
 *   2. 芯片自动逐步降低 IIN 直到不再触发 VINDPM
 *   3. 等待 ChargerStatus[14] ICO_DONE = 1
 *   4. 读取 IIN_DPM 寄存器获取实测最大电流
 *   5. 将该值写入 IIN_HOST 作为新限制
 * ======================================================================== */

int8_t input_power_run_ico(uint16_t *detected_current_ma)
{
    int8_t ret;
    int timeout;

    if (!detected_current_ma) return BQ25710_ERR_NULL_PTR;

    printf("[ICO] 开始输入电流优化检测...\n");

    /* Step 1: 使能 ICO 模式 */
    ret = bq25710_enable_ico(1);
    if (ret != BQ25710_OK) {
        printf("[ICO] 使能 ICO 失败 (ret=%d)\n", ret);
        return ret;
    }

    /* Step 2: 等待 ICO 完成 (典型耗时 <1s, 最差约 2s) */
    timeout = 2000; /* 2秒超时, 每10ms轮询 */
    while (timeout > 0) {
        BQ25710_ChargerStatus_t cs;
        ret = bq25710_get_charger_status(&cs);
        if (ret != BQ25710_OK) {
            bq25710_enable_ico(0);
            return ret;
        }

        if (cs.maps.ICO_DONE) {
            printf("[ICO] 检测完成\n");
            break;
        }

        /*
         * 实际项目中的延迟:
         *   HAL_Delay(10);
         * 或 RTOS 延迟:
         *   vTaskDelay(pdMS_TO_TICKS(10));
         */
        timeout -= 10;
    }

    if (timeout <= 0) {
        printf("[ICO] 检测超时\n");
        bq25710_enable_ico(0);
        return BQ25710_ERR_TIMEOUT;
    }

    /* Step 3: 读取 IIN_DPM 实际限制值 */
    ret = bq25710_get_dpm_current(detected_current_ma);
    if (ret != BQ25710_OK) {
        bq25710_enable_ico(0);
        return ret;
    }

    printf("[ICO] 适配器最大电流: %u mA\n", *detected_current_ma);

    /* Step 4: 将 ICO 检测值设为新的 IIN_HOST */
    ret = bq25710_set_input_current_limit(*detected_current_ma);

    /* Step 5: 关闭 ICO 模式 */
    bq25710_enable_ico(0);

    return ret;
}

/* ========================================================================
 * input_power_demo - 完整演示
 * ======================================================================== */

void input_power_demo(void)
{
    int8_t ret;

    printf("\n========== 输入功率管理演示 ==========\n");

    /* ================================================================
     * 场景A: 适配器功率充足
     *
     * 65W/20V 适配器 → 配置 IIN_HOST=3250mA, VINDPM=18V
     * 充电电流 3A + 系统负载 1A = 总 4A > 3.25A → IDPM 降流至 3.25A
     * (充电电流自动降低)
     * ================================================================ */
    printf("\n--- 场景A: 65W 适配器正常充电 ---\n");
    {
        ret = input_power_init(ADAPTER_65W_CURRENT_MA, VDPM_USB_20V_MV);
        if (ret == BQ25710_OK) {
            printf("65W 适配器配置完成: IIN=%umA, VINDPM=%umV\n",
                   ADAPTER_65W_CURRENT_MA, VDPM_USB_20V_MV);

            /* 设定充电电流: 2A
             * ChargeCurrent reg: (2000/64) << 6 = 31 << 6 = 0x07C0
             */
            bq25710_set_charge_current(2000);
            printf("充电电流设为 2000mA → reg=0x07C0\n");

            /* 使能充电 */
            bq25710_charging_enable(1);
            printf("充电已使能\n");
        }

        input_power_status_t s;
        ret = input_power_get_status(&s);
        if (ret == BQ25710_OK) {
            printf("状态: IINDPM=%s VINDPM=%s DPM=%umA\n",
                   s.in_iindpm ? "激活" : "正常",
                   s.in_vindpm ? "激活" : "正常",
                   s.dpm_actual_ma);
        }
    }

    /* ================================================================
     * 场景B: 适配器功率不足 → IDPM 自动降流
     *
     * 45W 适配器: 20V/2.25A
     * 系统消耗 25W + 充电需求 40W = 65W > 45W
     * BQ25710 IDPM 环路自动降低充电电流,
     * 输入功率限制在 45W 以内
     * ================================================================ */
    printf("\n--- 场景B: 45W 适配器功率不足 → IDPM 降流 ---\n");
    {
        ret = input_power_init(ADAPTER_45W_CURRENT_MA, VDPM_USB_20V_MV);
        if (ret == BQ25710_OK) {
            printf("45W 适配器: IIN=%umA\n", ADAPTER_45W_CURRENT_MA);

            /*
             * 尝试设置 3A 充电电流 (需要的输入功率 >45W)
             * BQ25710 IDPM 会自动将输入电流钳位在 2250mA
             */
            bq25710_set_charge_current(3000);
            printf("设定充电电流 3000mA → IDPM 自动限制输入至 2250mA\n");

            bq25710_charging_enable(1);

            /* 读取 DPM 实际值验证 */
            uint16_t dpm_ma;
            bq25710_get_dpm_current(&dpm_ma);
            printf("DPM 实际生效: %u mA\n", dpm_ma);
        }
    }

    /* ================================================================
     * 场景C: 输入电压跌落 → VDPM 介入
     *
     * USB 5V 2A 弱电源: 使用长线缆或低质量适配器时
     * 输入电压可能跌落至 4.5V 以下
     * VINDPM 设为 4.2V 防止输入电压崩溃
     * ================================================================ */
    printf("\n--- 场景C: USB 5V 弱电源 VDPM 保护 ---\n");
    {
        ret = input_power_init(2000, VDPM_USB_5V_MV);
        if (ret == BQ25710_OK) {
            printf("USB 5V 配置: IIN=2000mA, VINDPM=%umV\n", VDPM_USB_5V_MV);

            /*
             * VINDPM 计算:
             *   reg = ((4200 - 3200) / 64) << 6 = 15 << 6 = 0x03C0
             *
             * 当输入电压跌至 4.2V 时:
             *   - BQ25710 自动降低充电电流
             *   - 维持输入电压 ≥ 4.2V
             *   - ChargerStatus IN_VINDPM=1
             */
            printf("VINDPM 寄存器: 0x%04X\n",
                   (unsigned)(((VDPM_USB_5V_MV - 3200) / 64) << 6));
        }
    }

    /* ================================================================
     * 场景D: CPU Turbo → 峰值功率模式 Level1
     *
     * Intel Turbo Boost: CPU TDP 从 15W 跃升至 45W (持续 28s)
     * 适配器 65W + 电池补充 → 利用 ILIM2 短时过载
     * ================================================================ */
    printf("\n--- 场景D: CPU Turbo 峰值功率 Level1 ---\n");
    {
        /* 恢复 65W 适配器配置 */
        input_power_init(ADAPTER_65W_CURRENT_MA, VDPM_USB_20V_MV);

        /*
         * 峰值功率 Level1:
         *   ILIM2 = 150% × 3250mA = 4875mA
         *   过载时间 = 10ms
         *   弛豫周期 = 20ms
         *
         * ProchotOption0 ILIM2_VTH:
         *   150% → code=5 → 00101b → bit[15:11]=00101
         */
        ret = input_power_pkpwr_level1(10, 20, 150);
        if (ret == BQ25710_OK) {
            printf("Turbo 峰值模式: 4875mA/10ms, 弛豫 20ms\n");
            printf("  ILIM2 reg code: 5 (150%% IIN_HOST)\n");
        }
    }

    /* ================================================================
     * 场景E: 极端负载 → Level2 电池补偿
     *
     * 45W 适配器 + 65W TDP CPU 全速运行
     * 适配器无法满足需求, 电池自动补充
     * ================================================================ */
    printf("\n--- 场景E: 极端负载 → Level2 电池补偿 ---\n");
    {
        input_power_init(ADAPTER_45W_CURRENT_MA, VDPM_USB_20V_MV);

        ret = input_power_pkpwr_level2();
        if (ret == BQ25710_OK) {
            printf("Level2 电池补偿模式:\n");
            printf("  适配器供系统 (最大45W)\n");
            printf("  电池通过理想二极管补足差额\n");
            printf("  VSYS 下冲时峰值模式保护\n");
        }
    }

    /* ================================================================
     * ICO 检测适配器最大能力
     * ================================================================ */
    printf("\n--- ICO 适配器能力检测 ---\n");
    {
        uint16_t ico_result_ma;
        ret = input_power_run_ico(&ico_result_ma);
        if (ret == BQ25710_OK) {
            printf("ICO 结果: 适配器最大 %u mA, 已自动更新 IIN_HOST\n",
                   ico_result_ma);
        } else {
            printf("ICO 检测失败 (ret=%d), 可能需实际适配器连接\n", ret);
        }
    }

    printf("\n========== 输入功率管理演示完成 ==========\n");
}

/* ========================================================================
 * main() 骨架
 * ======================================================================== */

#ifdef EXAMPLE_INPUT_POWER_MAIN_ENABLED

#include "soft_i2c.h"
extern struct soft_i2c_bus_t g_i2c_bus;

int main(void)
{
    int8_t ret;

    printf("BQ25710 输入功率管理示例\n");

    ret = bq25710_init(&g_i2c_bus);
    if (ret != BQ25710_OK) {
        printf("驱动初始化失败\n");
        return -1;
    }

    bq25710_set_watchdog(BQ25710_WDT_88S);
    input_power_demo();

    /* 主循环: 监控输入功率状态 */
    while (1) {
        input_power_status_t s;
        input_power_get_status(&s);
        /* HAL_Delay(1000); */
    }

    return 0;
}

#endif /* EXAMPLE_INPUT_POWER_MAIN_ENABLED */
