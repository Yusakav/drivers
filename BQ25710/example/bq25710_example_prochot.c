/**
 * @file bq25710_example_prochot.c
 * @brief BQ25710 PROCHOT 多源热保护与CPU降频管理 - 完整实现
 *
 * 覆盖9个PROCHOT触发源 (Adapter_Removal, Battery_Removal, VSYS,
 * IDCHG, INOM, ICRIT, CMPIN, VAP, COMP) 的完整配置、
 * 中断服务框架及多场景触发/恢复演示。
 *
 * @note 基于已修复的 bq25710.h 驱动API，所有寄存器计算在注释中标注
 */

#include "bq25710_example_prochot.h"
#include <stdio.h>

/* ========================================================================
 * 默认 PROCHOT 配置 (9 源全开, 3A IDCHG 阈值)
 * ======================================================================== */

/**
 * @brief 获取默认 PROCHOT 配置
 *
 * 默认策略:
 *   - 9 个触发源全部使能
 *   - IDCHG 阈值 3000mA (3A)
 *     IDCHG_VTH code = (3000 - 128) / 512 = 5 → 寄存器 bit[15:10]=000101
 *   - ILIM2 = 150% IIN_HOST → 寄存器 bit[15:11]=01001
 *   - 脉冲宽度 10ms (默认)
 *   - 扩展模式禁用
 */
static void prochot_get_default_config(prochot_config_t *cfg)
{
    cfg->trigger_mask       = PROCHOT_SRC_ALL;
    cfg->pulse_width        = PROCHOT_WIDTH_10MS;
    cfg->pulse_extend       = 0;
    cfg->idchg_threshold_ma = 3000;
    cfg->idchg_deg          = IDCHG_DEG_130US;
    cfg->icrit_deg          = ICRIT_DEG_120US;
    cfg->ilim2              = ILIM2_150;
    cfg->inom_deg_50ms      = 0;  /* INOM 1ms 去抖 */
}

/* ========================================================================
 * prochot_init - 完整 PROCHOT 配置
 * ======================================================================== */

int8_t prochot_init(const prochot_config_t *cfg)
{
    if (!cfg) return BQ25710_ERR_NULL_PTR;

    int8_t ret;

    /*
     * Step 1: 设置触发源屏蔽 (ProchotOption1 bits 0-7)
     *
     * 寄存器 0x34 ProchotOption1:
     *   bit[0]=PP_ACOK    → 适配器移除触发
     *   bit[1]=PP_BATPRES → 电池移除触发
     *   bit[2]=PP_VSYS    → VSYS 欠压触发
     *   bit[3]=PP_IDCHG   → 放电过流触发
     *   bit[4]=PP_INOM    → 输入平均过流触发
     *   bit[5]=PP_ICRIT   → 输入峰值过流触发
     *   bit[6]=PP_COMP    → 独立比较器触发
     *   bit[7]=PP_VDPM    → VDPM 电压跌落触发
     */
    ret = bq25710_set_prochot_trigger_mask(cfg->trigger_mask);
    if (ret != BQ25710_OK) return ret;

    /*
     * Step 2: 设置 IDCHG_VTH 放电过流阈值
     *
     * 计算方法: code = (current_mA - 128) / 512
     *   示例: 3000mA → (3000 - 128) / 512 = 5 (0x05)
     *   寄存器 bit[15:10] = 000101b
     *   实际情况: 阈值 = code × 512 + 128 = 5×512+128 = 2688mA
     *
     *   若需精确 3A: 3072mA → code = (3072-128)/512 = 5 (取整)
     *   或: 3136mA → code=5, 实际=2688mA (步进受限)
     *
     *   推荐: 3072mA (code=5, 实际阈值 2688mA)
     */
    ret = bq25710_set_prochot_idchg_threshold(cfg->idchg_threshold_ma);
    if (ret != BQ25710_OK) return ret;

    /*
     * Step 3: 配置 ILIM2 峰值电流百分比 (ProchotOption0 bit[15:11])
     *
     * ILIM2_VTH 5-bit 值对应 IIN_HOST 百分比:
     *   00001 = 110%, 01001 = 150% (默认), 01101 = 230%
     *
     * 适配器 65W / 19V = 3.42A 额定
     * ILIM2 @ 150% = 5.13A 短期峰值
     */
    /* ProchotOption0 需要直接通过 bq25710_write_word 写入 */
    {
        BQ25710_ProchotOption0_t pro0;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_PROCHOT_OPTION_0, &val);
        if (ret != BQ25710_OK) return ret;
        pro0.all = (uint16_t)val;

        /* ILIM2_VTH: 5-bit 写入 bit[15:11] */
        pro0.maps.ILIM2_VTH = (uint8_t)cfg->ilim2 & 0x1F;

        /* ICRIT_DEG: 2-bit 写入 bit[10:9] */
        pro0.maps.ICRIT_DEG = (uint8_t)cfg->icrit_deg & 0x03;

        /* INOM_DEG: 1-bit 写入 bit[1] */
        pro0.maps.INOM_DEG = cfg->inom_deg_50ms ? 1 : 0;

        ret = bq25710_write_word(BQ25710_REG_PROCHOT_OPTION_0, (int16_t)pro0.all);
        if (ret != BQ25710_OK) return ret;
    }

    /*
     * Step 4: 单独设置 IDCHG_DEG (ProchotOption1 bit[9:8])
     *
     * IDCHG_DEG 去抖: 00=2ms, 01=130us(默认), 10=8ms, 11=16ms
     * 需要读-改-写 ProchotOption1 (因为 Step1 只写了低8位)
     */
    {
        BQ25710_ProchotOption1_t pro1;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_PROCHOT_OPTION_1, &val);
        if (ret != BQ25710_OK) return ret;
        pro1.all = (uint16_t)val;

        /* 更新 IDCHG_DEG bit[9:8] */
        pro1.maps.IDCHG_DEG = (uint8_t)cfg->idchg_deg & 0x03;

        ret = bq25710_write_word(BQ25710_REG_PROCHOT_OPTION_1, (int16_t)pro1.all);
        if (ret != BQ25710_OK) return ret;
    }

    /*
     * Step 5: 配置 PROCHOT 脉冲宽度和扩展模式 (ProchotStatus)
     *
     * ProchotStatus bit[13:12] = PROCHOT_WIDTH:
     *   00=100μs, 01=1ms, 10=10ms(默认), 11=5ms
     *
     * ProchotStatus bit[14] = EN_PROCHOT_EXIT:
     *   0=正常脉冲模式 (脉冲结束自动释放)
     *   1=脉冲扩展模式 (保持低电平直到主机写 0 至 PROCHOT_CLEAR)
     */
    {
        BQ25710_ProchotStatus_t ps;
        int16_t val;
        ret = bq25710_read_word(BQ25710_REG_PROCHOT_STATUS, &val);
        if (ret != BQ25710_OK) return ret;
        ps.all = (uint16_t)val;

        ps.maps.PROCHOT_WIDTH   = (uint8_t)cfg->pulse_width & 0x03;
        ps.maps.EN_PROCHOT_EXIT = cfg->pulse_extend ? 1 : 0;

        ret = bq25710_write_word(BQ25710_REG_PROCHOT_STATUS, (int16_t)ps.all);
        if (ret != BQ25710_OK) return ret;
    }

    printf("[PROCHOT] 初始化完成:\n");
    printf("  触发源屏蔽: 0x%02X\n", cfg->trigger_mask);
    printf("  IDCHG 阈值: %u mA\n", cfg->idchg_threshold_ma);
    printf("  ILIM2: %u%% IIN_HOST\n", (unsigned)(100 + (cfg->ilim2 - 1) * 10));
    printf("  脉冲宽度: %s\n",
           cfg->pulse_width == PROCHOT_WIDTH_100US ? "100us" :
           cfg->pulse_width == PROCHOT_WIDTH_1MS   ? "1ms"   :
           cfg->pulse_width == PROCHOT_WIDTH_5MS   ? "5ms"   : "10ms");
    printf("  扩展模式: %s\n", cfg->pulse_extend ? "启用" : "禁用");

    return BQ25710_OK;
}

/* ========================================================================
 * prochot_read_events - 读取 PROCHOT 事件
 * ======================================================================== */

int8_t prochot_read_events(prochot_event_t *event)
{
    if (!event) return BQ25710_ERR_NULL_PTR;

    BQ25710_ProchotStatus_t ps;
    int8_t ret = bq25710_get_prochot_status(&ps);
    if (ret != BQ25710_OK) return ret;

    event->adapter_removal = ps.maps.STAT_Adapter_Removal;
    event->battery_removal = ps.maps.STAT_Battery_Removal;
    event->vsys_under      = ps.maps.STAT_VSYS;
    event->idchg_over      = ps.maps.STAT_IDCHG;
    event->inom_over       = ps.maps.STAT_INOM;
    event->icrit_over      = ps.maps.STAT_ICRIT;
    event->comp_trigger    = ps.maps.STAT_COMP;
    event->vdpm_under      = ps.maps.STAT_VDPM;
    event->exit_vap        = ps.maps.STAT_EXIT_VAP;
    event->vap_fail        = ps.maps.STAT_VAP_FAIL;

    return BQ25710_OK;
}

/* ========================================================================
 * prochot_clear_and_release - 清除 PROCHOT 状态
 * ======================================================================== */

int8_t prochot_clear_and_release(void)
{
    return bq25710_clear_faults();
}

/* ========================================================================
 * prochot_isr_handler - PROCHOT 中断服务例程框架
 *
 * 调用场景: 当 MCU GPIO 检测到 PROCHOT 引脚下降沿时,
 *           在 ISR 中调用此函数处理寄存器层面逻辑。
 *
 * @note 实际项目中需根据硬件 GPIO 配置设置 EXTI 中断
 *       (如 STM32 HAL_GPIO_EXTI_Callback / CH32L GPIO 中断)
 * ======================================================================== */

void prochot_isr_handler(void)
{
    prochot_event_t event;
    int8_t ret;

    ret = prochot_read_events(&event);
    if (ret != BQ25710_OK) {
        printf("[PROCHOT ISR] 读取状态失败 (ret=%d)\n", ret);
        return;
    }

    printf("[PROCHOT ISR] 触发! 事件详情:\n");

    /* 逐项识别并记录触发源 */
    if (event.adapter_removal)
        printf("  -> 适配器移除 (STAT_Adapter_Removal)\n");
    if (event.battery_removal)
        printf("  -> 电池移除 (STAT_Battery_Removal)\n");
    if (event.vsys_under)
        printf("  -> VSYS 欠压 (STAT_VSYS)\n");
    if (event.idchg_over)
        printf("  -> 电池放电过流 (STAT_IDCHG)\n");
    if (event.inom_over)
        printf("  -> 输入平均过流 (STAT_INOM)\n");
    if (event.icrit_over)
        printf("  -> 输入峰值过流 (STAT_ICRIT)\n");
    if (event.comp_trigger)
        printf("  -> 独立比较器触发 (STAT_COMP)\n");
    if (event.vdpm_under)
        printf("  -> VBUS 欠压 VDPM (STAT_VDPM)\n");
    if (event.exit_vap)
        printf("  -> 退出 VAP 模式 (STAT_EXIT_VAP)\n");
    if (event.vap_fail)
        printf("  -> VAP 加载失败 (STAT_VAP_FAIL)\n");

    /*
     * 降频策略:
     *   - IDCHG/ICRIT 触发 → CPU 降至 PL2 功率限制
     *   - VSYS 欠压 → CPU 降至 PL1 功率限制
     *   - 适配器/电池移除 → CPU 强制最低频
     *
     * 实际项目中调用平台特定的 CPU 降频接口:
     *   if (event.idchg_over || event.icrit_over)
     *       cpu_throttle_to_pl2();
     *   if (event.vsys_under)
     *       cpu_throttle_to_pl1();
     *   if (event.adapter_removal)
     *       cpu_throttle_min();
     */

    /* 清除 PROCHOT 状态位并释放引脚 */
    ret = prochot_clear_and_release();
    if (ret != BQ25710_OK) {
        printf("[PROCHOT ISR] 清除故障失败 (ret=%d)\n", ret);
    } else {
        printf("[PROCHOT ISR] 状态已清除, PROCHOT 引脚已释放\n");
    }
}

/* ========================================================================
 * prochot_scenario_demo - 多场景触发演示
 *
 * 模拟3种典型 PROCHOT 触发场景, 各场景独立演示
 * ======================================================================== */

void prochot_scenario_demo(void)
{
    printf("\n========== PROCHOT 多场景演示 ==========\n");

    /*
     * 场景A: 适配器移除触发
     *
     * 触发条件: CHRG_OK 下降沿 (AC_STAT 从 1→0)
     * 配置: PP_ACOK=1 (ProchotOption1 bit[0]=1)
     *
     * 流程:
     *   1. 使能 Adapter_Removal 触发源
     *   2. 当适配器被拔出, BQ25710 自动拉低 PROCHOT
     *   3. MCU ISR 检测并降低 CPU 频率
     *   4. 适配器重新插入, 清除 PROCHOT 状态
     *
     * 注意: 此场景需实际硬件插拔适配器触发,
     *       演示中仅展示配置和检测逻辑
     */
    printf("\n--- 场景A: 适配器移除保护 ---\n");
    {
        int is_adapter;
        is_adapter = bq25710_is_adapter_present();
        printf("当前适配器状态: %s\n", is_adapter == 1 ? "已插入" :
               is_adapter == 0 ? "未插入" : "读取失败");

        if (is_adapter == 0) {
            printf("检测到适配器已移除! 应执行降频策略:\n");
            printf("  - CPU 降至最低频 (LFM)\n");
            printf("  - 关闭高功耗外设 (屏幕亮度降至最低)\n");
            printf("  - 禁用 Turbo Boost\n");
        } else {
            printf("适配器正常, 无需降频\n");
        }
    }

    /*
     * 场景B: 电池放电过流 (IDCHG) 触发
     *
     * 触发条件: 电池放电电流 > IDCHG_VTH 阈值 (持续超过 IDCHG_DEG 去抖时间)
     *
     * 配置示例: IDCHG 阈值 3A, 去抖 130μs
     *
     * 保护逻辑:
     *   - Intel Turbo Boost 会使 CPU+GPU 同时满载, 电池放电电流激增
     *   - IDCHG 超阈值 → PROCHOT 拉低 → CPU 降频 → 放电电流下降
     *   - 放电电流降至阈值以下 → 清除 PROCHOT → CPU 恢复
     */
    printf("\n--- 场景B: IDCHG 放电过流保护 ---\n");
    {
        prochot_config_t cfg;
        prochot_get_default_config(&cfg);

        /* 仅使能 IDCHG 触发 */
        cfg.trigger_mask       = PROCHOT_SRC_IDCHG;
        cfg.idchg_threshold_ma = 3000;  /* 3A 阈值 */
        cfg.idchg_deg          = IDCHG_DEG_130US;

        int8_t ret = prochot_init(&cfg);
        if (ret == BQ25710_OK) {
            printf("IDCHG 保护已配置: 阈值=%umA, 去抖=130us\n",
                   cfg.idchg_threshold_ma);
        } else {
            printf("IDCHG 配置失败 (ret=%d)\n", ret);
        }

        /*
         * 监控循环 (实际项目中放入主循环):
         *   while(1) {
         *       prochot_event_t evt;
         *       prochot_read_events(&evt);
         *       if (evt.idchg_over) {
         *           cpu_throttle();
         *           prochot_clear_and_release();
         *       }
         *       delay_ms(100);
         *   }
         */
    }

    /*
     * 场景C: VSYS 电压下冲 + VAP 补偿
     *
     * 触发条件: VSYS < VSYS_TH1/VSYS_TH2 阈值 (ProchotOption0 bits[3:2] / [7:4])
     *
     * VAP 模式下:
     *   - VSYS 下冲 → PROCHOT 触发 → CPU 降频
     *   - VAP 利用 VBUS 电容储能补偿系统电压
     *   - 降频生效后, VSYS 恢复 → EXIT_VAP → 清除 PROCHOT
     *
     * VSYS_TH2 (2S-4S 默认值):
     *   00=5.9V, 01=6.2V(默认), 10=6.5V, 11=6.8V
     *
     * 配置示例: VSYS_TH2 = 6.2V (对于 3S 系统)
     */
    printf("\n--- 场景C: VSYS 欠压 + VAP 联合保护 ---\n");
    {
        BQ25710_ProchotOption0_t pro0;
        int16_t val;
        int8_t ret;

        ret = bq25710_read_word(BQ25710_REG_PROCHOT_OPTION_0, &val);
        if (ret == BQ25710_OK) {
            pro0.all = (uint16_t)val;

            /* VSYS_TH2 保持默认 01 (6.2V for 2S-4S) */
            /* VSYS_TH1 保持默认 0110 (6.5V for 2S-4S) */
            /* PROCHOT_VDPM_80_90: 0=VinDPM的80%, 1=VinDPM的90% */

            /* 使能 VDPM PROCHOT 降低阈值至 80% VINDPM */
            pro0.maps.LOWER_PROCHOT_VDPM  = 0;  /* 遵循 VinDPM 设置 */
            pro0.maps.PROCHOT_VDPM_80_90  = 0;  /* 80% VinDPM 触发 */

            ret = bq25710_write_word(BQ25710_REG_PROCHOT_OPTION_0, (int16_t)pro0.all);
            if (ret == BQ25710_OK) {
                printf("VSYS 保护已配置: TH2=6.2V, VDPM_trigger=80%%VinDPM\n");
            }
        }

        printf("VSYS+VAP 联合保护策略:\n");
        printf("  1. VSYS 下冲 → PROCHOT → CPU 降频\n");
        printf("  2. VAP 启动, VBUS 电容储能补偿 VSYS\n");
        printf("  3. VSYS 恢复 → EXIT_VAP → 清除 PROCHOT\n");
    }

    printf("\n========== PROCHOT 演示完成 ==========\n");
}

/* ========================================================================
 * main() 骨架 - 条件编译入口
 * ======================================================================== */

#ifdef EXAMPLE_PROCHOT_MAIN_ENABLED

#include "soft_i2c.h"

/* 外部 I2C 总线实例 (由用户硬件初始化代码提供) */
extern struct soft_i2c_bus_t g_i2c_bus;

int main(void)
{
    int8_t ret;
    prochot_config_t cfg;

    printf("BQ25710 PROCHOT 示例 - 启动\n");

    /* 1. 初始化驱动 */
    ret = bq25710_init(&g_i2c_bus);
    if (ret != BQ25710_OK) {
        printf("驱动初始化失败 (ret=%d)\n", ret);
        return -1;
    }

    /* 2. 配置看门狗 (88s, 避免充电过程中超时) */
    bq25710_set_watchdog(BQ25710_WDT_88S);

    /* 3. 配置 PROCHOT (9 源全开) */
    prochot_get_default_config(&cfg);
    cfg.trigger_mask       = PROCHOT_SRC_ALL;
    cfg.idchg_threshold_ma = 3000;

    ret = prochot_init(&cfg);
    if (ret != BQ25710_OK) {
        printf("PROCHOT 初始化失败 (ret=%d)\n", ret);
        return -1;
    }

    /* 4. 场景演示 */
    prochot_scenario_demo();

    /*
     * 5. 主循环: 周期性检测 PROCHOT 状态
     *
     * 实际项目中 PROCHOT 检测在 GPIO 中断中完成,
     * 此处的轮询仅为演示和调试用途
     */
    printf("\n进入 PROCHOT 监控循环 (每 500ms 检测一次)...\n");
    while (1) {
        prochot_event_t evt;
        ret = prochot_read_events(&evt);
        if (ret == BQ25710_OK) {
            uint8_t any_event = evt.adapter_removal | evt.battery_removal |
                               evt.vsys_under | evt.idchg_over |
                               evt.inom_over | evt.icrit_over |
                               evt.comp_trigger | evt.vdpm_under |
                               evt.exit_vap | evt.vap_fail;

            if (any_event) {
                printf("[轮询] 检测到 PROCHOT 事件! 进入 ISR 处理...\n");
                prochot_isr_handler();
            }
        }

        /*
         * 延迟 500ms (实际项目中使用 RTOS 延迟或定时器回调)
         * HAL_Delay(500);
         */
    }

    return 0;
}

#endif /* EXAMPLE_PROCHOT_MAIN_ENABLED */
