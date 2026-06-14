/**
 * @file    bq25710_charge_example.c
 * @author  nyarukov (luckychaoyue1@gmail.com)
 * @brief   BQ25710 ���������Ӧ��ʾ�� ���� ʵ���ļ�
 * @version 1.0
 * @date    2026-06-11
 *
 * @details ���ļ�ʵ����:
 *          1. ������� (LiFePO4) / ��Ԫ� (NMC) 1S~4S ��ز�����ѯ
 *          2. �������״̬�� (����Ԥ���CC��CV����ɡ����ϴ���)
 *          3. ״̬�����������
 *          4. OTG ������� (5V/9V/12V/15V/20V)
 *          5. main() �����Ǽ�ʾ��
 *
 *          ����:
 *          - bq25710.h / bq25710.c (���޸�������)
 *          - bq25710_abstraction.h (�Ĵ��������嶨��)
 *          - logger.h (��־��)
 *
 *          ���Ź�: ι�����ڱ��� < 88s (WDTMR_ADJ = 10b)����ʱ�������
 *          �ᱻӲ�����㣬���������� ChargeCurrent �Ĵ�����
 *
 * @copyright Copyright (c) 2026
 */

#include "bq25710_charge_example.h"
#include "bq25710_abstraction.h"
#include "logger.h"

#include <string.h>   /* memset */
#include <stdio.h>    /* printf */

/* ģ�鼶��־��ǩ���� */
#ifdef LOG_TAG
#undef LOG_TAG
#endif
#define LOG_TAG "BQ25710_EX"


/* ================================================================
 *  �ڲ������뾲̬����
 * ================================================================ */

/** @brief ���׶������ַ��� (������־���) */
static const char *s_stage_names[] = {
    [BQ25710_CHARGE_STAGE_IDLE]           = "IDLE",
    [BQ25710_CHARGE_STAGE_DETECTION]      = "DETECTION",
    [BQ25710_CHARGE_STAGE_PRECHARGE]      = "PRECHARGE",
    [BQ25710_CHARGE_STAGE_FAST_CHARGE_CC] = "CC",
    [BQ25710_CHARGE_STAGE_FAST_CHARGE_CV] = "CV",
    [BQ25710_CHARGE_STAGE_DONE]           = "DONE",
    [BQ25710_CHARGE_STAGE_FAULT]          = "FAULT",
};

/** @brief ���Ź�ι������ (ms) �� ȡ 80s������ 88s ��ʱ��ֵ�Ա�֤���� */
#define WDT_FEED_INTERVAL_MS  80000U

/** @brief CV �׶ν����о�: VBAT �� VREG �� (98/100) ��Ϊ�����ѹ�� */
#define CV_ENTRY_THRESHOLD_PERCENT  98U

/** @brief ������������ */
#define FAULT_RETRY_MAX  6U


/* ================================================================
 *  1. ��ز������ұ�
 *
 *  �Ա����ڳ���������ʽ��֯ 2 �� chemistry �� 4 �� cell_count = 8 ��
 *  ����������: [battery_type][cell_count - 1]
 * ================================================================ */

static const bq25710_battery_params_t s_param_table[2][4] = {

    /* ========== LiFePO4 (�������) ========== */
    [BQ25710_BATTERY_TYPE_LIFEPO4] = {
        /* 1S */
        [0] = {
            .vreg_per_cell_mv  = BQ25710_PARAM_1S_LFP_VREG_PER_CELL_MV,
            .charge_voltage_mv = BQ25710_PARAM_1S_LFP_CHARGE_VOLTAGE_MV,
            .min_sys_voltage_mv= BQ25710_PARAM_1S_LFP_MIN_SYS_VOLTAGE_MV,
            .batlowv_mv        = BQ25710_PARAM_1S_LFP_BATLOWV_MV,
            .charge_current_ma = BQ25710_PARAM_1S_LFP_CHARGE_CURRENT_MA,
            .term_current_ma   = BQ25710_PARAM_1S_LFP_TERM_CURRENT_MA,
            .sysovp_mv         = BQ25710_PARAM_1S_LFP_SYSOVP_MV,
        },
        /* 2S */
        [1] = {
            .vreg_per_cell_mv  = BQ25710_PARAM_2S_LFP_VREG_PER_CELL_MV,
            .charge_voltage_mv = BQ25710_PARAM_2S_LFP_CHARGE_VOLTAGE_MV,
            .min_sys_voltage_mv= BQ25710_PARAM_2S_LFP_MIN_SYS_VOLTAGE_MV,
            .batlowv_mv        = BQ25710_PARAM_2S_LFP_BATLOWV_MV,
            .charge_current_ma = BQ25710_PARAM_2S_LFP_CHARGE_CURRENT_MA,
            .term_current_ma   = BQ25710_PARAM_2S_LFP_TERM_CURRENT_MA,
            .sysovp_mv         = BQ25710_PARAM_2S_LFP_SYSOVP_MV,
        },
        /* 3S */
        [2] = {
            .vreg_per_cell_mv  = BQ25710_PARAM_3S_LFP_VREG_PER_CELL_MV,
            .charge_voltage_mv = BQ25710_PARAM_3S_LFP_CHARGE_VOLTAGE_MV,
            .min_sys_voltage_mv= BQ25710_PARAM_3S_LFP_MIN_SYS_VOLTAGE_MV,
            .batlowv_mv        = BQ25710_PARAM_3S_LFP_BATLOWV_MV,
            .charge_current_ma = BQ25710_PARAM_3S_LFP_CHARGE_CURRENT_MA,
            .term_current_ma   = BQ25710_PARAM_3S_LFP_TERM_CURRENT_MA,
            .sysovp_mv         = BQ25710_PARAM_3S_LFP_SYSOVP_MV,
        },
        /* 4S */
        [3] = {
            .vreg_per_cell_mv  = BQ25710_PARAM_4S_LFP_VREG_PER_CELL_MV,
            .charge_voltage_mv = BQ25710_PARAM_4S_LFP_CHARGE_VOLTAGE_MV,
            .min_sys_voltage_mv= BQ25710_PARAM_4S_LFP_MIN_SYS_VOLTAGE_MV,
            .batlowv_mv        = BQ25710_PARAM_4S_LFP_BATLOWV_MV,
            .charge_current_ma = BQ25710_PARAM_4S_LFP_CHARGE_CURRENT_MA,
            .term_current_ma   = BQ25710_PARAM_4S_LFP_TERM_CURRENT_MA,
            .sysovp_mv         = BQ25710_PARAM_4S_LFP_SYSOVP_MV,
        },
    },

    /* ========== Li-ion NMC (��Ԫ�) ========== */
    [BQ25710_BATTERY_TYPE_NMC] = {
        /* 1S */
        [0] = {
            .vreg_per_cell_mv  = BQ25710_PARAM_1S_NMC_VREG_PER_CELL_MV,
            .charge_voltage_mv = BQ25710_PARAM_1S_NMC_CHARGE_VOLTAGE_MV,
            .min_sys_voltage_mv= BQ25710_PARAM_1S_NMC_MIN_SYS_VOLTAGE_MV,
            .batlowv_mv        = BQ25710_PARAM_1S_NMC_BATLOWV_MV,
            .charge_current_ma = BQ25710_PARAM_1S_NMC_CHARGE_CURRENT_MA,
            .term_current_ma   = BQ25710_PARAM_1S_NMC_TERM_CURRENT_MA,
            .sysovp_mv         = BQ25710_PARAM_1S_NMC_SYSOVP_MV,
        },
        /* 2S */
        [1] = {
            .vreg_per_cell_mv  = BQ25710_PARAM_2S_NMC_VREG_PER_CELL_MV,
            .charge_voltage_mv = BQ25710_PARAM_2S_NMC_CHARGE_VOLTAGE_MV,
            .min_sys_voltage_mv= BQ25710_PARAM_2S_NMC_MIN_SYS_VOLTAGE_MV,
            .batlowv_mv        = BQ25710_PARAM_2S_NMC_BATLOWV_MV,
            .charge_current_ma = BQ25710_PARAM_2S_NMC_CHARGE_CURRENT_MA,
            .term_current_ma   = BQ25710_PARAM_2S_NMC_TERM_CURRENT_MA,
            .sysovp_mv         = BQ25710_PARAM_2S_NMC_SYSOVP_MV,
        },
        /* 3S */
        [2] = {
            .vreg_per_cell_mv  = BQ25710_PARAM_3S_NMC_VREG_PER_CELL_MV,
            .charge_voltage_mv = BQ25710_PARAM_3S_NMC_CHARGE_VOLTAGE_MV,
            .min_sys_voltage_mv= BQ25710_PARAM_3S_NMC_MIN_SYS_VOLTAGE_MV,
            .batlowv_mv        = BQ25710_PARAM_3S_NMC_BATLOWV_MV,
            .charge_current_ma = BQ25710_PARAM_3S_NMC_CHARGE_CURRENT_MA,
            .term_current_ma   = BQ25710_PARAM_3S_NMC_TERM_CURRENT_MA,
            .sysovp_mv         = BQ25710_PARAM_3S_NMC_SYSOVP_MV,
        },
        /* 4S */
        [3] = {
            .vreg_per_cell_mv  = BQ25710_PARAM_4S_NMC_VREG_PER_CELL_MV,
            .charge_voltage_mv = BQ25710_PARAM_4S_NMC_CHARGE_VOLTAGE_MV,
            .min_sys_voltage_mv= BQ25710_PARAM_4S_NMC_MIN_SYS_VOLTAGE_MV,
            .batlowv_mv        = BQ25710_PARAM_4S_NMC_BATLOWV_MV,
            .charge_current_ma = BQ25710_PARAM_4S_NMC_CHARGE_CURRENT_MA,
            .term_current_ma   = BQ25710_PARAM_4S_NMC_TERM_CURRENT_MA,
            .sysovp_mv         = BQ25710_PARAM_4S_NMC_SYSOVP_MV,
        },
    },
};


/* ================================================================
 *  �ڲ��������� (static)
 * ================================================================ */

/**
 * @brief ���ó���ѹ���� 8mV ����ȡ��
 *
 * MaxChargeVoltage �Ĵ��� (SMBus ��ַ 15h):
 *   λ [14:3] ��Ч��λ [2:0] ���� (д 0)
 *   reg_value = (voltage_mV / 8) << 3
 *   LSB = 8mV����Χ 1.024V ~ 19.2V
 *
 * @param voltage_mV  Ŀ������ֹ��ѹ (mV)
 * @return int8_t     ״̬��
 */
static int8_t charge_set_vreg(uint16_t voltage_mV)
{
    /*
     * �ֲ� Page 43: MaxChargeVoltage ���� 8mV
     *   ����ѹ = 1.024V + REG �� 8mV
     *   �� REG = (voltage_mV - 1024) / 8
     *   �Ĵ���λӳ��: REG << 3
     *
     * ��: bq25710_set_max_charge_voltage() �ڲ����� (mV/8)<<3 ת����
     *   ֱ�ӵ��ü��ɡ�
     */
    return bq25710_set_max_charge_voltage(voltage_mV);
}

/**
 * @brief ���ó��������� 64mA ����ȡ��
 *
 * ChargeCurrent �Ĵ��� (SMBus ��ַ 14h):
 *   λ [12:6] ��Ч��λ [5:0] ���� (��������ֵ)
 *   reg_value = (current_mA / 64) << 6
 *   LSB = 64mA����Χ 0 ~ 8128mA (10m�� ��������)
 *
 * @param current_mA  Ŀ������� (mA)
 * @return int8_t     ״̬��
 */
static int8_t charge_set_ichg(uint16_t current_mA)
{
    return bq25710_set_charge_current(current_mA);
}

/**
 * @brief ������Сϵͳ��ѹ���� 256mV ����ȡ��
 *
 * MinSystemVoltage �Ĵ��� (SMBus ��ַ 3Eh):
 *   λ [13:8] ��Ч��λ [7:0] ����
 *   reg_value = (voltage_mV / 256) << 8
 *   LSB = 256mV
 *
 * @param voltage_mV  Ŀ����Сϵͳ��ѹ (mV)
 * @return int8_t     ״̬��
 */
static int8_t charge_set_minsys(uint16_t voltage_mV)
{
    return bq25710_set_min_system_voltage(voltage_mV);
}

/**
 * @brief ִ��һ�ο��Ź�ι�� (��д ChargeCurrent �Ĵ���)
 *
 * BQ25710 ���Ź���ʱ��Ὣ ChargeCurrent ����Ϊ 0mA��
 * ���������Ե��ڳ�ʱǰ�� ChargeCurrent ִ��һ��д������ι����
 *
 * @param current_mA  ��ǰӦ���ֵĳ����� (mA)
 * @return int8_t     ״̬��
 */
static int8_t charge_feed_watchdog(uint16_t current_mA)
{
    return bq25710_set_charge_current(current_mA);
}

/**
 * @brief �׶�Ǩ���ڲ����� (����־)
 *
 * @param ctx       ���������
 * @param new_stage Ŀ��׶�
 * @param now_ms    ��ǰʱ���
 */
static void charge_transition_to(bq25710_charge_ctx_t *ctx,
                                 bq25710_charge_stage_t new_stage,
                                 uint32_t               now_ms)
{
    ctx->prev_stage     = ctx->stage;
    ctx->stage          = new_stage;
    ctx->stage_enter_ms = now_ms;
    ctx->fault_retry_cnt = 0;

    LOG_I("[CHARGE] �׶�Ǩ��: %s �� %s (t=%lu ms)",
          s_stage_names[ctx->prev_stage],
          s_stage_names[ctx->stage],
          (unsigned long)now_ms);
}


/* ================================================================
 *  2. API ʵ��: ��ز�����ѯ
 * ================================================================ */

/**
 * @brief ���ݵ�����ͺͽ�����ȡ��Ӧ�Ĳ�����
 */
int8_t bq25710_get_battery_params(bq25710_battery_type_t   battery_type,
                                  batt_cell_count_t     cell_count,
                                  bq25710_battery_params_t *params)
{
    if (params == NULL) {
        return BQ25710_ERR_NULL_PTR;
    }

    if (battery_type > BQ25710_BATTERY_TYPE_NMC ||
        cell_count < BQ25710_CELL_COUNT_1S ||
        cell_count > BQ25710_CELL_COUNT_4S) {
        LOG_E("��Ч�ĵ�ز���: type=%d, cells=%d", battery_type, cell_count);
        return BQ25710_ERR_INVALID_PARAM;
    }

    /* ����: cell_count - 1 (0-based) */
    *params = s_param_table[battery_type][cell_count - 1];

    LOG_I("��ز�����ѯ: type=%s, %dS, Vreg=%u mV, ICHG=%u mA",
          (battery_type == BQ25710_BATTERY_TYPE_LIFEPO4) ? "LiFePO4" : "NMC",
          (int)cell_count,
          params->charge_voltage_mv,
          params->charge_current_ma);

    return BQ25710_OK;
}


/* ================================================================
 *  3. API ʵ��: ����ʼ��
 * ================================================================ */

/**
 * @brief ��ʼ����������Ĳ����ó����Ӳ��
 *
 * @details ��������:
 *   - Step 1: ��ѯ������ (��ֹ��ѹ��ϵͳ��С��ѹ����������)
 *   - Step 2: д MaxChargeVoltage �Ĵ��� (0x15)
 *          BQ25710 Ĭ��ֵ: 1S=4200mV / 2S=8400mV / 3S=12600mV / 4S=16800mV
 *          ������ﮱ����дΪ��Ӧ�ĵ͵�ѹ (3.6V/cell)
 *          ��Ԫ� 4.2V/cell ��Ĭ��ֵһ�£���ֱ�����û���ʽ����
 *   - Step 3: д MinSystemVoltage �Ĵ��� (0x3E)
 *   - Step 4: ���ÿ��Ź�Ϊ 88s ��ʱ (WDTMR_ADJ = 10b)
 *   - Step 5: д ChargeCurrent = 0mA, CHRG_INHIBIT=1 (�Ƚ�ֹ���)
 *   - Step 6: ʹ�� ADC �������� (VBAT/VSYS/ICHG/IIN ͨ��)
 *   - Step 7: ��ʼ��������, ���� DETECTION �׶�
 */
int8_t bq25710_charge_init(bq25710_charge_ctx_t  *ctx,
                           bq25710_battery_type_t  battery_type,
                           batt_cell_count_t    cell_count,
                           uint32_t                now_ms)
{
    int8_t ret;

    if (ctx == NULL) {
        return BQ25710_ERR_NULL_PTR;
    }

    /* ---- ��������� ---- */
    memset(ctx, 0, sizeof(bq25710_charge_ctx_t));

    /* ---- Step 1: ��ѯ������ ---- */
    ret = bq25710_get_battery_params(battery_type, cell_count, &ctx->params);
    if (ret != BQ25710_OK) {
        LOG_E("����ʼ��ʧ��: ������ѯ���� (ret=%d)", ret);
        return ret;
    }
    ctx->battery_type = battery_type;
    ctx->cell_count   = cell_count;

    /* ---- Step 2: ���ó����ֹ��ѹ (MaxChargeVoltage) ---- */
    /*
     * �Ĵ������� (�ֲ� Page 43):
     *   ChargeVoltage() = 1.024V + REG �� 8mV
     *   �� ChargeVoltage = 4.200V (NMC 1S):
     *     REG = (4200 - 1024) / 8 = 397
     *     reg_value = 397 << 3 = 0x0C68
     *   �� ChargeVoltage = 3.600V (LiFePO4 1S):
     *     REG = (3600 - 1024) / 8 = 322
     *     reg_value = 322 << 3 = 0x0A10
     *
     * �������Ĭ�ϳ���ѹ����Ԫ﮲�ͬ��������ʽ��д��
     */
    ret = charge_set_vreg(ctx->params.charge_voltage_mv);
    if (ret != BQ25710_OK) {
        LOG_E("���ó����ֹ��ѹʧ��: %u mV (ret=%d)",
              ctx->params.charge_voltage_mv, ret);
        return ret;
    }
    LOG_I("MaxChargeVoltage = %u mV (Vreg_per_cell = %u mV, %dS %s)",
          ctx->params.charge_voltage_mv,
          ctx->params.vreg_per_cell_mv,
          (int)ctx->cell_count,
          (ctx->battery_type == BQ25710_BATTERY_TYPE_LIFEPO4) ? "LiFePO4" : "NMC");

    /* ---- Step 3: ������Сϵͳ��ѹ (MinSystemVoltage) ---- */
    /*
     * �Ĵ������� (�ֲ� Page 45):
     *   MinSystemVoltage() = 1.024V + REG �� 256mV
     *   �� MinSysVoltage = 3.584V (NMC 1S):
     *     REG = (3584 - 1024) / 256 = 10
     *     reg_value = 10 << 8 = 0x0A00
     *   �� MinSysVoltage = 3.072V (LiFePO4 1S):
     *     REG = (3072 - 1024) / 256 = 8
     *     reg_value = 8 << 8 = 0x0800
     */
    ret = charge_set_minsys(ctx->params.min_sys_voltage_mv);
    if (ret != BQ25710_OK) {
        LOG_E("������Сϵͳ��ѹʧ��: %u mV (ret=%d)",
              ctx->params.min_sys_voltage_mv, ret);
        return ret;
    }
    LOG_I("MinSystemVoltage = %u mV", ctx->params.min_sys_voltage_mv);

    /* ---- Step 4: ���ÿ��Ź� ---- */
    /*
     * ���Ź�ѡ�� (ChargeOption0[14:13] WDTMR_ADJ):
     *   00b = ����
     *   01b = 5s
     *   10b = 88s   �� �Ƽ� (��������, ι������ 80s)
     *   11b = 175s
     * ��ʱ�� ChargeCurrent �Զ����� �� ���ֹͣ �� ����д�ָ���
     */
    ret = bq25710_set_watchdog(BQ25710_WDT_88S);
    if (ret != BQ25710_OK) {
        LOG_E("���Ź�����ʧ�� (ret=%d)", ret);
        return ret;
    }

    /* ---- Step 5: ��ʼ״̬: ��ֹ��� + ������ = 0 ---- */
    ret = bq25710_charging_enable(0);   /* CHRG_INHIBIT = 1 */
    if (ret != BQ25710_OK) {
        LOG_E("��ֹ���ʧ�� (ret=%d)", ret);
        return ret;
    }

    /* ---- Step 6: ʹ�� ADC �������� ---- */
    /*
     * ֻʹ�ܱ�Ҫͨ���Խ�ʡ����:
     *   VBAT + VSYS + ICHG + IIN (�� VBUS �������������)
     * ����ģʽ: ADC_CONV=1, ÿ���Զ�ˢ�¡�
     */
    ret = bq25710_adc_configure(
        BQ25710_ADC_VBAT | BQ25710_ADC_VSYS |
        BQ25710_ADC_ICHG | BQ25710_ADC_IIN |
        BQ25710_ADC_VBUS,
        1   /* continuous_mode = 1 */
    );
    if (ret != BQ25710_OK) {
        LOG_W("ADC ����ʧ�� (ret=%d), ״̬�����ܲ�����", ret);
        /* ����ֹ��ʼ������ */
    }

    /* ---- Step 7: ��ʼ��������, ���� DETECTION �׶� ---- */
    ctx->stage            = BQ25710_CHARGE_STAGE_IDLE;
    ctx->prev_stage       = BQ25710_CHARGE_STAGE_IDLE;
    ctx->stage_enter_ms   = now_ms;
    ctx->total_charge_ms  = 0;
    ctx->last_wdt_feed_ms = now_ms;
    ctx->fault_retry_cnt  = 0;
    ctx->last_error       = BQ25710_OK;

    charge_transition_to(ctx, BQ25710_CHARGE_STAGE_DETECTION, now_ms);

    LOG_I("����ʼ�����: %dS %s, �ȴ�����������...",
          (int)cell_count,
          (battery_type == BQ25710_BATTERY_TYPE_LIFEPO4) ? "LiFePO4" : "NMC");

    return BQ25710_OK;
}


/* ================================================================
 *  4. API ʵ��: ���״̬����ѭ��
 * ================================================================ */

/**
 * @brief ���״̬����ѭ��
 *
 * @warning �������������Ե��� (�Ƽ� 500ms~1s ���)��
 *          ���ü���������ܵ��¿��Ź���ʱ����״̬��Ӧ����ʱ��
 *
 * @details ״̬������ͼ:
 *
 *   ������������������������
 *   ��  IDLE    ������(�û��������)������ DETECTION
 *   ������������������������
 *        ��
 *        ��                                      ������ VBAT < BATLOWV ������ PRECHARGE
 *   �����������ة�����������       ��������������������������            ��
 *   �� DETECTION������������������  ��ȡ ADC  ��������������������������
 *   ������������������������       �� ��ȡStatus��            ������ VBAT �� BATLOWV ������ CC
 *                      ��������������������������
 *                            ��
 *              �����������������������������੤��������������������������
 *              ��             ��             ��
 *         PRECHARGE         CC            CV
 *         (LDOģʽ)    (�������)     (��ѹ���)
 *              ��             ��             ��
 *              �� VBAT��       �� VBAT��       �� ICHG��
 *              �� BATLOWV     �� 98%VREG     �� C/10
 *              ��             ��             ��
 *              CC            CV           DONE
 *                                            ��
 *         ������������������������������������������������������������������������
 *         ��
 *   ������������������������     ���������
 *   ��  FAULT   ������������������������������������ ԭ�׶�
 *   ������������������������
 *        ��
 *   ����׶� (fault_mask �� 0)
 */
bq25710_charge_stage_t bq25710_charge_run(bq25710_charge_ctx_t *ctx,
                                          uint32_t               now_ms)
{
    if (ctx == NULL) {
        return BQ25710_CHARGE_STAGE_IDLE;
    }

    /* ---- ��һ��: ���´����� (ADC + Status) ---- */
    bq25710_battery_status_monitor(ctx, now_ms);

    /* ---- �ڶ���: ����Ƿ����¹��� ---- */
    if (ctx->fault_mask != 0) {
        if (ctx->stage != BQ25710_CHARGE_STAGE_FAULT) {
            LOG_W("[CHARGE] ��⵽����! mask=0x%04X, ���� FAULT �׶�", ctx->fault_mask);
            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_FAULT, now_ms);
        }
        /* �� FAULT �׶��м������� */
    }

    /* ---- ������: ���ݵ�ǰ�׶�ִ���߼� ---- */
    switch (ctx->stage) {

    /* ================================================================
     * �׶�0: �����ʶ�� (DETECTION)
     *
     * ����������Ƿ���� (AC_STAT)������Ƿ���� (CELL_BATPRESZ)��
     * ��ȡ VBAT �жϵ�ǰ��ѹ�����ĸ����׶Ρ�
     *
     * ��������: ��ʼ����ɺ�
     * �뿪����: AC �ѽ��� �� ���� VBAT ��ת�� PRECHARGE �� CC
     * ================================================================ */
    case BQ25710_CHARGE_STAGE_DETECTION:
    {
        /* ����������Ƿ���� */
        if (!ctx->chg_status.maps.AC_STAT) {
            /* ��������, �����ȴ� (��Ƶ����ӡ, ÿ 60s ���һ��) */
            static uint32_t last_nosrc_log_ms = 0;
            if (now_ms - last_nosrc_log_ms > 60000U) {
                LOG_D("�ȴ�����������...");
                last_nosrc_log_ms = now_ms;
            }
            break;
        }

        LOG_I("�������ѽ���, VBAT=%u mV, VSYS=%u mV",
              ctx->adc.vbat_mV, ctx->adc.vsys_mV);

        /* ���� VBAT ���������ĸ��׶� */
        if (ctx->adc.vbat_mV < ctx->params.batlowv_mv) {
            /*
             * VBAT < BATLOWV: ������ؿ���, ����Ԥ��� (LDO ģʽ)
             * Ӳ�����Զ��� BATFET �л�������ģʽ, ����ǯλ ~384mA (1S)
             * ����� (2S~4S LDO ģʽ)��
             *
             * �ؼ��Ĵ�������:
             *   - EN_LDO = 1 (ChargeOption0[2], Ĭ��ֵ)
             *   - ChargeVoltage = VREG (���� init ������)
             *   - ChargeCurrent = ���ͳ����� (IC = 1C)
             *   - CHRG_INHIBIT = 0 (ʹ�ܳ��)
             *
             * Ӳ����Ϊ:
             *   - VBAT < BATLOWV ʱ�Զ�����Ԥ���
             *   - IN_PCHRG λ = 1 (Ԥ��״̬)
             *   - ������Ӳ��ǯλ, ChargeCurrent �Ĵ����趨�ĵ���
             *     ��Ԥ��׶β���Ч (�� LDO ǯλ����)
             */
            LOG_I("��ؿ��� (VBAT=%u mV < BATLOWV=%u mV), ����Ԥ���",
                  ctx->adc.vbat_mV, ctx->params.batlowv_mv);

            /* ȷ�� LDO ģʽʹ�� (Ӧ����Ĭ��ֵ, ����ʽ���ø���ȫ) */
            bq25710_set_ldo_mode(1);

            /* д������� (��ʹԤ��׶α� LDO ǯλ, ҲΪ CC �׶���׼��) */
            charge_set_ichg(ctx->params.charge_current_ma);

            /* д������ֹ��ѹ */
            charge_set_vreg(ctx->params.charge_voltage_mv);

            /* �������! */
            ctx->last_error = bq25710_charging_enable(1);
            if (ctx->last_error != BQ25710_OK) {
                LOG_E("����Ԥ���ʧ�� (ret=%d)", ctx->last_error);
                charge_transition_to(ctx, BQ25710_CHARGE_STAGE_FAULT, now_ms);
                break;
            }

            ctx->last_wdt_feed_ms = now_ms;
            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_PRECHARGE, now_ms);
        } else {
            /*
             * VBAT �� BATLOWV: ��ص�ѹ����, ֱ�ӽ��������� (CC)
             */
            LOG_I("��ص�ѹ���� (VBAT=%u mV �� BATLOWV=%u mV), ����������",
                  ctx->adc.vbat_mV, ctx->params.batlowv_mv);

            charge_set_ichg(ctx->params.charge_current_ma);
            charge_set_vreg(ctx->params.charge_voltage_mv);

            ctx->last_error = bq25710_charging_enable(1);
            if (ctx->last_error != BQ25710_OK) {
                LOG_E("�����������ʧ�� (ret=%d)", ctx->last_error);
                charge_transition_to(ctx, BQ25710_CHARGE_STAGE_FAULT, now_ms);
                break;
            }

            ctx->last_wdt_feed_ms = now_ms;
            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_FAST_CHARGE_CC, now_ms);
        }
        break;
    }

    /* ================================================================
     * �׶�1: Ԥ��� (PRECHARGE) �� VBAT < BATLOWV
     *
     * Ӳ�� LDO Ԥ���ģʽ, ������Ӳ���Զ�ǯλ��
     * ����ְ��: ��� VBAT ����, �� VBAT �� BATLOWV ʱǨ�Ƶ� CC �׶Ρ�
     *
     * BQ25710 Ԥ�����Ϊ (�ֲ� Section 8.3.3.2):
     *   - 1S: BATFET ����ģʽ, ICHG �� 384mA (����ֵ)
     *   - 2S~4S: �л�ģʽԤ���, ������Ӳ������
     *   - IN_PCHRG = 1 ��ʾ����Ԥ���״̬
     * ================================================================ */
    case BQ25710_CHARGE_STAGE_PRECHARGE:
    {
        /* ι�� (��д ChargeCurrent) */
        if (now_ms - ctx->last_wdt_feed_ms >= WDT_FEED_INTERVAL_MS) {
            charge_feed_watchdog(ctx->params.charge_current_ma);
            ctx->last_wdt_feed_ms = now_ms;
            LOG_D("Ԥ���׶�ι��, VBAT=%u mV", ctx->adc.vbat_mV);
        }

        /* ��� VBAT �Ƿ�ﵽ BATLOWV */
        if (ctx->adc.vbat_mV >= ctx->params.batlowv_mv) {
            LOG_I("Ԥ������! VBAT=%u mV �� BATLOWV=%u mV, ���� CC �׶�",
                  ctx->adc.vbat_mV, ctx->params.batlowv_mv);

            /*
             * Ԥ����ɺ�Ӳ�����Զ��л��� CC ģʽ��
             * ��������Ҫȷ�� ChargeCurrent ����ȷд�� (������Ԥ��׶�
             * ��Ӳ������), ������Ǩ��ǰ��дһ�Ρ�
             */
            charge_set_ichg(ctx->params.charge_current_ma);
            ctx->last_wdt_feed_ms = now_ms;

            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_FAST_CHARGE_CC, now_ms);
        }
        /* Ԥ��糬ʱ���� (30 ����) */
        else if (now_ms - ctx->stage_enter_ms > 1800000U) {
            LOG_E("Ԥ��糬ʱ! VBAT=%u mV ��δ�ﵽ BATLOWV=%u mV, ��ؿ�������",
                  ctx->adc.vbat_mV, ctx->params.batlowv_mv);

            bq25710_charging_enable(0);  /* ֹͣ��� */
            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_FAULT, now_ms);
        }
        break;
    }

    /* ================================================================
     * �׶�2: ������� (CC) �� BATLOWV �� VBAT < VREG
     *
     * Ӳ�����趨 ChargeCurrent �������, VBAT ����������
     * �� VBAT �ӽ� VREG (�� 98%) ʱ, Ӳ���Զ��л�Ϊ��ѹģʽ (CV),
     * ��ʱ��������ʼ�½���
     *
     * Ǩ���о�: VBAT �� charge_voltage �� 98% �� ���� CV �׶�
     *
     * BQ25710 CC��CV �л�:
     *   Ӳ���ڲ��Ƚ������ VBAT, �� VBAT �� VREG ʱ�Զ��� CC
     *   �л��� CV ģʽ��IN_FCHRG �� CC �ڼ� = 1, ���� CV ��
     *   IN_FCHRG ������Ϊ 1 ֱ�����������½���
     *   ����ͨ�� VBAT ��ѹ + �����½�����˫���ж� CV �׶Ρ�
     * ================================================================ */
    case BQ25710_CHARGE_STAGE_FAST_CHARGE_CC:
    {
        /* ι�� */
        if (now_ms - ctx->last_wdt_feed_ms >= WDT_FEED_INTERVAL_MS) {
            charge_feed_watchdog(ctx->params.charge_current_ma);
            ctx->last_wdt_feed_ms = now_ms;
            LOG_D("CC �׶�ι��, VBAT=%u mV, ICHG=%u mA",
                  ctx->adc.vbat_mV, ctx->adc.ichg_mA);
        }

        /*
         * ����Ƿ���� CV �׶�:
         *   �о�: VBAT �� charge_voltage �� 98%
         *
         *   ���� NMC 4S: charge_voltage = 16800 mV
         *     ��ֵ = 16800 �� 98 / 100 = 16464 mV
         *   ���� LiFePO4 4S: charge_voltage = 14400 mV
         *     ��ֵ = 14400 �� 98 / 100 = 14112 mV
         */
        uint32_t cv_threshold_mv =
            (uint32_t)ctx->params.charge_voltage_mv * CV_ENTRY_THRESHOLD_PERCENT / 100U;

        if (ctx->adc.vbat_mV >= cv_threshold_mv) {
            LOG_I("���� CV �׶�! VBAT=%u mV �� CV��ֵ=%lu mV",
                  ctx->adc.vbat_mV, (unsigned long)cv_threshold_mv);

            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_FAST_CHARGE_CV, now_ms);
        }

        /* CC �׶γ�ʱ���� (4 Сʱ) */
        if (now_ms - ctx->stage_enter_ms > 14400000U) {
            LOG_E("CC �׶γ�ʱ! VBAT=%u mV, �������ֹ", ctx->adc.vbat_mV);
            bq25710_charging_enable(0);
            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_FAULT, now_ms);
        }
        break;
    }

    /* ================================================================
     * �׶�3: ��ѹ��� (CV) �� VBAT �� VREG, �������½�
     *
     * �����ά�� VBAT = VREG, ������ (ICHG) ��ʱ�����½���
     * �� ICHG �� ��ֹ���� (C/10) ʱ, �����ɡ�
     *
     * ��ֹ��������:
     *   C/10 = charge_current_ma / 10
     *   ���� NMC 4S charge_current = 2000mA �� C/10 = 200mA
     *
     * ע��: ADC ICHG LSB ȡ���� IBAT_GAIN ����:
     *   IBAT_GAIN = 0 (16x, Ĭ��): LSB = 64mA
     *   IBAT_GAIN = 1 (8x):       LSB = 128mA
     *   �ڵ͵��� (< 512mA) ��������ʹ�� 16x ����߾��ȡ�
     * ================================================================ */
    case BQ25710_CHARGE_STAGE_FAST_CHARGE_CV:
    {
        /* ι�� (CV �׶�����Ҫ, ��Ϊ ChargeCurrent > 0) */
        if (now_ms - ctx->last_wdt_feed_ms >= WDT_FEED_INTERVAL_MS) {
            charge_feed_watchdog(ctx->params.charge_current_ma);
            ctx->last_wdt_feed_ms = now_ms;
            LOG_D("CV �׶�ι��, VBAT=%u mV, ICHG=%u mA",
                  ctx->adc.vbat_mV, ctx->adc.ichg_mA);
        }

        /*
         * ����Ƿ�ﵽ��ֹ����: ICHG �� C/10
         */
        if (ctx->adc.ichg_mA <= ctx->params.term_current_ma) {
            LOG_I("������! ICHG=%u mA �� C/10=%u mA, VBAT=%u mV",
                  ctx->adc.ichg_mA,
                  ctx->params.term_current_ma,
                  ctx->adc.vbat_mV);

            /* ֹͣ���: ChargeCurrent = 0 �� CHRG_INHIBIT = 1 */
            bq25710_charging_enable(0);
            ctx->total_charge_ms += (now_ms - ctx->stage_enter_ms);

            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_DONE, now_ms);
        }

        /* CV �׶γ�ʱ���� (2 Сʱ) */
        if (now_ms - ctx->stage_enter_ms > 7200000U) {
            LOG_W("CV �׶γ�ʱ, ICHG=%u mA δ���� %u mA, ǿ����ֹ",
                  ctx->adc.ichg_mA, ctx->params.term_current_ma);
            bq25710_charging_enable(0);
            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_DONE, now_ms);
        }
        break;
    }

    /* ================================================================
     * �׶�4: ������ (DONE)
     *
     * �������ֹ���������³�� (�������Էŵ��), ���ٴε���
     * bq25710_charge_init() ��ֱ����ת�� DETECTION �׶Ρ�
     * ================================================================ */
    case BQ25710_CHARGE_STAGE_DONE:
        /* ���־�ֹ, �ȴ��û�ָ�� */
        break;

    /* ================================================================
     * �׶�5: ���ϴ��� (FAULT)
     *
     * ��� ChargerStatus ����λ:
     *   - ACOV (bit7): �����ѹ �� ���������, �ȴ� VBUS �ָ�
     *   - ACOC (bit5): ������� �� ���ͳ�����������
     *   - BATOC (bit6): ��طŵ���� �� ��ͣ���, ֪ͨϵͳ
     *   - SYSOVP (bit4): ϵͳ��ѹ �� �������, ��д 0 ���
     *   - SYS_SHORT (bit3): ϵͳ��· �� 7 ������ʧ�ܺ�����
     *   - LATCHOFF (bit2): ǿ�Ƶ�Դ·���ر� �� �Ƚ�������
     *
     * ����:
     *   1. ��ȡ����¼��������
     *   2. �������λ (д 0 �� ChargerStatus ���ֽ�)
     *   3. ���ݹ������;����Ƿ�����
     *   4. ���Դ������޺���������, ���˹���Ԥ
     * ================================================================ */
    case BQ25710_CHARGE_STAGE_FAULT:
    {
        uint16_t fault = ctx->fault_mask;
        uint8_t  recoverable = 0;

        LOG_W("[FAULT] �������� = 0x%04X", fault);

        /* ��һ�������� */
        if (fault & (1U << 7)) {  /* ACOV: �����ѹ */
            LOG_E("  �� ACOV (�����ѹ): VBUS > 26V, ���������!");
            recoverable = 1;  /* �� VBUS �ָ������������ */
        }
        if (fault & (1U << 6)) {  /* BATOC: ��طŵ���� */
            LOG_E("  �� BATOC (��طŵ����): ��鸺��!");
            recoverable = 1;
        }
        if (fault & (1U << 5)) {  /* ACOC: ������� */
            LOG_E("  �� ACOC (�������): ��������� ILIM2 ����!");
            recoverable = 1;
        }
        if (fault & (1U << 4)) {  /* SYSOVP: ϵͳ��ѹ */
            /*
             * SYSOVP ��ֵ (Ӳ���̶�, �� CELL_BATPRESZ ����):
             *   1S: 5V
             *   2S: 12V
             *   3S/4S: 19.5V
             * ����������, ��д ChargerStatus[4]=0 ��������������
             */
            LOG_E("  �� SYSOVP (ϵͳ��ѹ): VSYS > %u mV, ����!",
                  ctx->params.sysovp_mv);
            recoverable = 0;  /* ���ع���, ���Զ����� */
        }
        if (fault & (1U << 3)) {  /* SYS_SHORT: ϵͳ��· */
            LOG_E("  �� SYS_SHORT (ϵͳ��·): VSYS < 2.4V, 7 ������ʧ��!");
            recoverable = 0;  /* ���˹���� */
        }
        if (fault & (1U << 2)) {  /* LATCHOFF: ǿ�ƹر� */
            LOG_E("  �� LATCHOFF (�Ƚ�������ǿ�ƹر�)!");
            recoverable = 0;
        }
        if (fault & (1U << 1)) {  /* OTG OVP */
            LOG_E("  �� OTG OVP (OTG ��ѹ)!");
            recoverable = 1;
        }
        if (fault & (1U << 0)) {  /* OTG UVP */
            LOG_E("  �� OTG UVP (OTG Ƿѹ)!");
            recoverable = 1;
        }

        /* ������й���λ */
        bq25710_clear_faults();

        /* ����: ���Ի����������� */
        if (recoverable && ctx->fault_retry_cnt < FAULT_RETRY_MAX) {
            ctx->fault_retry_cnt++;
            LOG_I("���Ͽɻָ�, �� %u/%u ������...",
                  ctx->fault_retry_cnt, FAULT_RETRY_MAX);

            /* �ص����׶� */
            bq25710_charging_enable(0);
            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_DETECTION, now_ms);
        } else {
            LOG_E("���ϲ��ɻָ������Դ����þ�, �������档���˹����Ӳ����");
            bq25710_charging_enable(0);
            /* ������ FAULT �׶� */
            
            charge_transition_to(ctx, BQ25710_CHARGE_STAGE_DETECTION, now_ms);
        }
        break;
    }

    /* ================================================================
     * �׶�: ���� (IDLE)
     *
     * ��ʼ״̬���û���ͣ��硣�����κβ�����
     * ================================================================ */
    case BQ25710_CHARGE_STAGE_IDLE:
    default:
        break;
    }

    return ctx->stage;
}


/* ================================================================
 *  5. API ʵ��: ״̬���
 * ================================================================ */

/**
 * @brief �ۺ�״̬��⺯��
 *
 * ÿ�ε��ø��� ctx �е�:
 *   - chg_status  (ChargerStatus �Ĵ���)
 *   - adc         (ADC �������)
 *   - fault_mask  (����λ����)
 *
 * ��ȡ˳��:
 *   1. ChargerStatus �� ��ȡ AC_STAT��IN_PCHRG��IN_FCHRG ��
 *   2. ADC ȫͨ�� �� ��ȡ VBAT��VSYS��ICHG��IIN��VBUS
 *   3. ������������
 */
void bq25710_battery_status_monitor(bq25710_charge_ctx_t *ctx,
                                    uint32_t               now_ms)
{
    if (ctx == NULL) return;

    int8_t ret;

    /* --- ��ȡ ChargerStatus (0x20) --- */
    memset(&ctx->chg_status, 0, sizeof(ctx->chg_status));
    ret = bq25710_get_charger_status(&ctx->chg_status);
    if (ret != BQ25710_OK) {
        ctx->last_error = ret;
        LOG_E("��ȡ ChargerStatus ʧ�� (ret=%d)", ret);
    }

    /* --- ��ȡ ADC ȫͨ�� --- */
    memset(&ctx->adc, 0, sizeof(ctx->adc));
    ret = bq25710_adc_read_all(&ctx->adc);
    if (ret != BQ25710_OK) {
        ctx->last_error = ret;
        /* ADC ��ȡʧ�ܲ���ֹ״̬������ */
    }

    /* --- ��ȡ�������� (ChargerStatus �� 8 λ) --- */
    ctx->fault_mask = ctx->chg_status.low_byte & 0x00FF;

    (void)now_ms; /* Ԥ��: δ��������ʱ�������߼� */
}


/**
 * @brief ��ӡ���йؼ�״̬��Ϣ
 */
void bq25710_print_status(const bq25710_charge_ctx_t *ctx)
{
    if (ctx == NULL) {
        printf("[BQ25710] ctx is NULL\n");
        return;
    }

    const char *type_str = (ctx->battery_type == BQ25710_BATTERY_TYPE_LIFEPO4)
                           ? "LiFePO4" : "NMC";

    printf("\n"
           "============================================================\n"
           "  BQ25710 ���״̬����\n"
           "============================================================\n");

    /* ������� */
    printf("  [�������]\n");
    printf("    ��ѧ����:     %s\n", type_str);
    printf("    ��о����:     %dS\n", (int)ctx->cell_count);
    printf("    �����ֹ��ѹ:  %u mV (%.2f V)\n",
           ctx->params.charge_voltage_mv,
           ctx->params.charge_voltage_mv / 1000.0f);
    printf("    ������:      %u mA\n", ctx->params.charge_current_ma);
    printf("    ��ֹ����(C/10): %u mA\n", ctx->params.term_current_ma);
    printf("    Ԥ�����ֵ:    %u mV\n", ctx->params.batlowv_mv);

    /* ������״̬ */
    printf("\n  [AC ������]\n");
    printf("    ������״̬:    %s\n",
           ctx->chg_status.maps.AC_STAT ? "�ѽ���" : "δ����");
    if (ctx->chg_status.maps.AC_STAT) {
        printf("    VBUS:          %u mV (%.2f V)\n",
               ctx->adc.vbus_mV, ctx->adc.vbus_mV / 1000.0f);
    }

    /* ���׶� */
    printf("\n  [���׶�]\n");
    printf("    ��ǰ�׶�:      %s\n", s_stage_names[ctx->stage]);
    printf("    Ԥ���״̬:    %s\n",
           ctx->chg_status.maps.IN_PCHRG ? "��" : "��");
    printf("    ���״̬:      %s\n",
           ctx->chg_status.maps.IN_FCHRG ? "��" : "��");
    printf("    DPM ����״̬:  IINDPM=%s VINDPM=%s\n",
           ctx->chg_status.maps.IN_IINDPM ? "��" : "��",
           ctx->chg_status.maps.IN_VINDPM ? "��" : "��");

    /* ��ѹ����� */
    printf("\n  [��ѹ�����]\n");
    printf("    VBAT:          %u mV (%.2f V)\n",
           ctx->adc.vbat_mV, ctx->adc.vbat_mV / 1000.0f);
    printf("    VSYS:          %u mV (%.2f V)\n",
           ctx->adc.vsys_mV, ctx->adc.vsys_mV / 1000.0f);
    printf("    ICHG:          %u mA\n", ctx->adc.ichg_mA);
    printf("    IIN:           %u mA\n", ctx->adc.iin_mA);
    printf("    IDCHG:         %u mA\n", ctx->adc.idchg_mA);

    /* ����״̬ */
    printf("\n  [���ϱ�־]\n");
    if (ctx->fault_mask == 0) {
        printf("    (�޹���)\n");
    } else {
        if (ctx->fault_mask & (1U << 7)) printf("    ! ACOV (�����ѹ)\n");
        if (ctx->fault_mask & (1U << 6)) printf("    ! BATOC (��طŵ����)\n");
        if (ctx->fault_mask & (1U << 5)) printf("    ! ACOC (�������)\n");
        if (ctx->fault_mask & (1U << 4)) printf("    ! SYSOVP (ϵͳ��ѹ, ����!)\n");
        if (ctx->fault_mask & (1U << 3)) printf("    ! SYS_SHORT (ϵͳ��·!)\n");
        if (ctx->fault_mask & (1U << 2)) printf("    ! LATCHOFF (ǿ�ƹر�!)\n");
        if (ctx->fault_mask & (1U << 1)) printf("    ! OTG OVP (OTG ��ѹ)\n");
        if (ctx->fault_mask & (1U << 0)) printf("    ! OTG UVP (OTG Ƿѹ)\n");
    }

    /* ͳ�� */
    printf("\n  [���ͳ��]\n");
    printf("    �ۼƳ��ʱ��:  %lu ms\n",
           (unsigned long)(ctx->total_charge_ms +
            (ctx->stage != BQ25710_CHARGE_STAGE_IDLE ?
             (0 /* ��: now_ms ����ⲿ���� */) : 0)));

    printf("============================================================\n\n");
}

/**
 * @brief �ж��Ƿ����ڳ�� (Ԥ�����)
 */
int bq25710_is_charging_ex(const bq25710_charge_ctx_t *ctx)
{
    if (ctx == NULL) return 0;

    return (ctx->stage == BQ25710_CHARGE_STAGE_PRECHARGE ||
            ctx->stage == BQ25710_CHARGE_STAGE_FAST_CHARGE_CC ||
            ctx->stage == BQ25710_CHARGE_STAGE_FAST_CHARGE_CV) ? 1 : 0;
}

/**
 * @brief �жϳ���Ƿ������
 */
int bq25710_is_charge_done_ex(const bq25710_charge_ctx_t *ctx)
{
    if (ctx == NULL) return 0;

    return (ctx->stage == BQ25710_CHARGE_STAGE_DONE) ? 1 : 0;
}


/* ================================================================
 *  6. API ʵ��: OTG �������
 *
 *  OTG ģʽʹ BQ25710 �ӵ��ȡ�硢������ѹ����� VBUS �˿ڡ�
 *
 *  �ؼ��Ĵ�������:
 *    ChargeOption3[2]  OTG_RANGE_LOW = 0 (������: 4.28V~20.8V)
 *                                      ���� 5V/9V/12V/15V/20V
 *    ChargeOption3[12] EN_OTG = 1 (ʹ�� OTG)
 *    OTGVoltage (0x3B): VBUS = 1.28V + REG �� 8mV (������)
 *    OTGCurrent (0x3C): IOUT = REG �� 50mA (10m�� ��������)
 *
 *  USB PD ��׼��ѹ:
 *    5V  ��  REG = (5000-1280)/8 = 465  �� 0x0744
 *    9V  ��  REG = (9000-1280)/8 = 965  �� 0x0F14
 *    12V ��  REG = (12000-1280)/8 = 1340 �� 0x14F0
 *    15V ��  REG = (15000-1280)/8 = 1715 �� 0x1ACC
 *    20V ��  REG = (20000-1280)/8 = 2340 �� 0x2490
 *
 *  ���� OTG ǰ�����Ƚ��ó�� (CHRG_INHIBIT=1), ���� EN_OTG д����Ч��
 * ================================================================ */

/**
 * @brief OTG ���ͨ�����ú���
 */
static int8_t otg_enable_voltage(uint16_t voltage_mV, uint16_t current_mA)
{
    int8_t ret;

    /* ���ó�� */
    ret = bq25710_charging_enable(0);
    if (ret != BQ25710_OK) {
        LOG_E("OTG: ���ó��ʧ�� (ret=%d)", ret);
        return ret;
    }

    /* ʹ�� OTG (�ڲ���ɵ�ѹ/�������� + EN_OTG=1) */
    ret = bq25710_enable_otg(voltage_mV, current_mA);
    if (ret != BQ25710_OK) {
        LOG_E("OTG: ʹ�� %u mV @ %u mA ʧ�� (ret=%d)",
              voltage_mV, current_mA, ret);
        return ret;
    }

    LOG_I("OTG ���������: %u mV (%.1f V) @ %u mA",
          voltage_mV, voltage_mV / 1000.0f, current_mA);

    return BQ25710_OK;
}

int8_t bq25710_otg_enable_5v(uint16_t current_ma)
{
    return otg_enable_voltage(BQ25710_OTG_VOUT_5V, current_ma);
}

int8_t bq25710_otg_enable_9v(uint16_t current_ma)
{
    return otg_enable_voltage(BQ25710_OTG_VOUT_9V, current_ma);
}

int8_t bq25710_otg_enable_12v(uint16_t current_ma)
{
    return otg_enable_voltage(BQ25710_OTG_VOUT_12V, current_ma);
}

int8_t bq25710_otg_enable_15v(uint16_t current_ma)
{
    return otg_enable_voltage(BQ25710_OTG_VOUT_15V, current_ma);
}

int8_t bq25710_otg_enable_20v(uint16_t current_ma)
{
    return otg_enable_voltage(BQ25710_OTG_VOUT_20V, current_ma);
}

/**
 * @brief �ر� OTG ģʽ���ָ����ģʽ
 *
 * @details ��� EN_OTG λ (ChargeOption3[12]=0)��оƬ�ص����ģʽ��
 *          ע��: �ر� OTG ����ȴ� VBUS �ŵ���ϲ�������������硣
 *          ����Ҫ�����µ��� bq25710_charge_init() �ָ���������ġ�
 */
int8_t bq25710_otg_disable(void)
{
    int8_t ret;

    ret = bq25710_otg_enable(0);   /* EN_OTG = 0 */
    if (ret != BQ25710_OK) {
        LOG_E("�ر� OTG ʧ�� (ret=%d)", ret);
        return ret;
    }

    LOG_I("OTG ģʽ�ѹر�");
    return BQ25710_OK;
}


/* ================================================================
 *  7. main() �����Ǽ�ʾ��
 *
 *  չʾ�����ĳ�繤������:
 *    1. ��ʼ�� I2C �� BQ25710 ����
 *    2. ѡ�������������
 *    3. ��ʼ�����������
 *    4. ��ѭ��: ����״̬�� + ������״̬����
 *    5. OTG ģʽ�л���ʾ
 *    6. ���ϴ�����ʾ
 *
 *  ��ʾ������:
 *    - I2C �����ѳ�ʼ�� (i2c_bus_init)
 *    - SysTick �ṩ���뼶ʱ���
 *    - Ӳ��: CELL_BATPRESZ ��ѹ������Ŀ�����ƥ��
 * ================================================================ */

// #define BQ25710_EXAMPLE_MAIN_ENABLED

#ifdef BQ25710_EXAMPLE_MAIN_ENABLED

#include <stdbool.h>

/* �ⲿϵͳ��������(���û�ʵ��) */
extern void     system_init(void);
extern uint32_t system_get_tick_ms(void);
extern void     system_delay_ms(uint32_t ms);

/**
 * @brief BQ25710 ���ʾ��������
 *
 * ��������:
 *   1. ������ʼ�� �� ���� ID У��
 *   2. ѡ����: 4S NMC ��Ԫ� (�ɸ�Ϊ LFP)
 *   3. ���״̬��ѭ��
 *   4. ������������������ OTG ģʽ
 */
int main(void)
{
    int8_t  ret;
    int     i2c_bus_handle;

    /* ---- ϵͳ��ʼ�� ---- */
    system_init();

    /* ---- I2C ���߳�ʼ�� (�û�ʵ��) ---- */
    i2c_bus_handle = 0;  /* ʾ��: I2C1 */

    /* ---- BQ25710 ������ʼ�� ---- */
    /*
     * bq25710_init() �ڲ�ִ��:
     *   - ���� I2C bus handle
     *   - ��ȡ DeviceID (0xFF) �� ManufactureID (0xFE)
     *   - У�� DeviceID ���ֽ� = 0x89
     *   - У�� ManufactureID = 0x0040
     *   - ���� s_initialized = 1
     */
    ret = bq25710_init(i2c_bus_handle);
    if (ret != BQ25710_OK) {
        printf("[FATAL] BQ25710 ������ʼ��ʧ�� (ret=%d)!\n"
               "  ����ԭ��:\n"
               "    1. I2C ����δ���ӻ��ַ���� (Ĭ�� 0x09)\n"
               "    2. оƬδ�ϵ�\n"
               "    3. SMBus ͨѶʱ������\n", ret);
        while (1) { /* ��ѭ��, �ȴ�Ӳ���Ų� */ }
    }
    printf("[OK] BQ25710 ������ʼ���ɹ�, ����ID=0x%04X\n",
           (unsigned int)BQ25710_DEVICE_ID_VAL);

    /* ================================================================
     *  ѡ��������
     *
     *  �޸��������м����л�������ͺͽ���:
     *
     *  ѡ��:
     *    LiFePO4 (�������):
     *      BQ25710_BATTERY_TYPE_LIFEPO4 + BQ25710_CELL_COUNT_4S
     *
     *    NMC (��Ԫ�):
     *      BQ25710_BATTERY_TYPE_NMC + BQ25710_CELL_COUNT_4S
     *
     *  ������� vs ��Ԫ﮲���:
     *    1. �����ֹ��ѹ: LiFePO4 = 3.60V/cell, NMC = 4.20V/cell
     *       ������ﮱ�����ʽ��д MaxChargeVoltage �Ĵ���!
     *       ��Ԫ� 4.20V/cell �� BQ25710 Ĭ��ֵһ�¡�
     *    2. ��Сϵͳ��ѹ: ��Ӧ���� (LiFePO4 1S=3.072V vs NMC 1S=3.584V)
     *    3. CELL_BATPRESZ ��ѹ��: 1S/2S/3S/4S ���ⲿ��ѹ����ƥ��
     *    4. SYSOVP ��ֵ: �� CELL_BATPRESZ Ӳ������
     *       1S=5V, 2S=12V, 3S/4S=19.5V
     * ================================================================ */
    bq25710_battery_type_t  battery_type = BQ25710_BATTERY_TYPE_NMC;
    batt_cell_count_t    cell_count   = BQ25710_CELL_COUNT_4S;

    /* ---- ��������� (ջ����, ��̬�����Գ־û�) ---- */
    static bq25710_charge_ctx_t charge_ctx;
    uint32_t now_ms = system_get_tick_ms();

    /* ---- ��ʼ����������Ӳ�� ---- */
    ret = bq25710_charge_init(&charge_ctx, battery_type, cell_count, now_ms);
    if (ret != BQ25710_OK) {
        printf("[FATAL] ����ʼ��ʧ�� (ret=%d)!\n", ret);
        while (1) {}
    }

    /* ---- ��ӡ��ʼ״̬ ---- */
    bq25710_print_status(&charge_ctx);

    /* ---- ��ѭ�� ---- */
    uint32_t last_print_ms  = now_ms;
    uint32_t last_stage_ms  = now_ms;
    bool     otg_mode       = false;

    printf("\n[BQ25710] ������ѭ��, �ȴ�����������...\n");
    printf("[BQ25710] ��ʾ: ��ģ�ⰴ���л� OTG ģʽ\n\n");

    while (1) {
        now_ms = system_get_tick_ms();

        /* ---- OTG ģʽ�л���ʾ (ģ�ⰴ���� GPIO ����) ---- */
        /*
         * ʾ��: ÿ 30 ���л�һ�� OTG ģʽ
         * ʵ��Ӧ����Ӧ�ɰ����� USB PD Э��ջ����
         */
#if 0  /* ��Ϊ 1 ������ OTG ��ʾ */
        static uint32_t last_otg_toggle_ms = 0;
        if (now_ms - last_otg_toggle_ms > 30000U) {
            last_otg_toggle_ms = now_ms;

            if (!otg_mode) {
                /* ���� OTG ģʽ: ��� 20V @ 3A */
                printf("\n--- �л��� OTG ģʽ (20V @ 3A) ---\n");
                ret = bq25710_otg_enable_20v(BQ25710_OTG_IOUT_3000MA);
                if (ret == BQ25710_OK) {
                    otg_mode = true;
                }
            } else {
                /* �˳� OTG ģʽ, �ָ���� */
                printf("\n--- �˳� OTG ģʽ, �ָ���� ---\n");
                bq25710_otg_disable();
                otg_mode = false;

                /* ���³�ʼ����� (�� DETECTION ��ʼ) */
                bq25710_charge_init(&charge_ctx, battery_type,
                                    cell_count, now_ms);
            }
        }
#endif

        /* ---- ���״̬�� (���ڷ� OTG ģʽ������) ---- */
        if (!otg_mode) {
            bq25710_charge_stage_t stage =
                bq25710_charge_run(&charge_ctx, now_ms);

            /* �׶α��ʱ��ӡ״̬ */
            if (stage != charge_ctx.prev_stage &&
                now_ms - last_stage_ms > 1000U) {
                bq25710_print_status(&charge_ctx);
                last_stage_ms = now_ms;
            }
        }

        /* ---- ������״̬��ӡ (ÿ 30 ��) ---- */
        if (now_ms - last_print_ms > 30000U) {
            if (!otg_mode) {
                bq25710_print_status(&charge_ctx);
            }
            last_print_ms = now_ms;
        }

        /* ---- ����������˹���Ԥ ---- */
        if (charge_ctx.stage == BQ25710_CHARGE_STAGE_FAULT &&
            charge_ctx.fault_retry_cnt >= FAULT_RETRY_MAX) {
            printf("[FAULT] ���ϲ��ɻָ�, ����Ӳ��������ϵͳ\n");
            /* ʵ��Ӧ���пɴ���ϵͳ��λ��֪ͨ�ϲ� */
            while (1) {
                system_delay_ms(1000);
            }
        }

        /* ---- �����ɴ��� ---- */
        if (charge_ctx.stage == BQ25710_CHARGE_STAGE_DONE) {
            /*
             * ���ڴ˴�:
             *   - ���� LED ָʾ����
             *   - �رճ��ָʾ��
             *   - ֪ͨ�ϲ�Ӧ��
             *   - ����͹���ģʽ
             */
        }

        system_delay_ms(500);  /* 500ms ѭ������ */
    }

    return 0;
}

#endif /* BQ25710_EXAMPLE_MAIN_ENABLED */
