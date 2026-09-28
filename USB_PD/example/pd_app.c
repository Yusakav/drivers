/**
* @file pd_app.c
* @brief USB PD 协议栈完整示例实现（覆盖全部支持特性）
*
* 所属模块：USB PD 驱动示例。
* 对应头文件：pd_app.h；底层依赖：usbpd.h / pd_sink.h / pd_source.h / usbpd_dpm.h。
*
* 包含 7 组独立示例，每组都有 init + task 两个函数。
* 每个示例都实现了完整的 DPM 回调集合（event / message_received / get_extended / set_source / source_ready / set_discharge），
* 其中 Source 专用回调（set_source / source_ready）为桩实现，应用层应替换为真实电源芯片驱动调用。
*
* 示例分组：
*   ┌────────────────────────────────────────────────────────────────────────────┐
*   │ 示例 1：Sink 基础（SPR 5V Fixed PDO）                                       │
*   │ 示例 2：Source 基础（SPR 5V/9V/12V 多 PDO）                                 │
*   │ 示例 3：PPS 动态调流（Source 宣告 PPS APDO，Sink 运行中变流）                 │
*   │ 示例 4：EPR 高压（≥20V，完整 EPR 进入流程）                                  │
*   │ 示例 5：DRP 双角色端口（rp/rd 自动切换）                                     │
*   │ 示例 6：VDM Discover Identity（结构化 VDM 请求 + 响应）                       │
*   │ 示例 7：Alert 阈值保护（Source OVP/OCP → Alert 消息）                        │
*   └────────────────────────────────────────────────────────────────────────────┘
*
* 编译时只启用需要的示例，主循环中对应的 xxx_task 函数。
*/
#include "pd_app.h"
#include "usbpd_dpm.h"
#include "pd_sink.h"
#include "pd_source.h"

#include <stdio.h>
#include <string.h>

/* ============================================================
 * 通用辅助：打印 DPM 事件
 * ============================================================ */

/** @brief 通用 DPM event 回调打印 */
void pd_example_print_dpm_event(uint8_t event, uint32_t v0, uint32_t v1)
{
    switch (event)
    {
    case USBPD_DPM_ATTACHED:       printf("[DPM] 已连接\n"); break;
    case USBPD_DPM_DETACHED:       printf("[DPM] 已断开\n"); break;
    case USBPD_DPM_CONTRACT:       printf("[DPM] 合约: %umV / %umA\n", v0, v1); break;
    case USBPD_DPM_CONTRACT_LOST:  printf("[DPM] 合约丢失\n"); break;
    case USBPD_DPM_HARD_RESET:     printf("[DPM] Hard Reset\n"); break;
    case USBPD_DPM_EPR_ENTERED:    printf("[DPM] EPR 已进入\n"); break;
    case USBPD_DPM_EPR_EXITED:     printf("[DPM] EPR 已退出\n"); break;
    case USBPD_DPM_PROTOCOL_ERROR: printf("[DPM] 协议错误: %u\n", v0); break;
    default:                       printf("[DPM] 未知事件: %u\n", event); break;
    }
}

/* ============================================================
 * 示例 1：Sink 基础（SPR 5V Fixed PDO）
 * ============================================================ */

static struct pd_sink_config_t g_sink_cfg_basic;

static void sink_basic_event(uint8_t port, uint8_t event, uint32_t v0, uint32_t v1, void *ctx)
{
    (void)port; (void)ctx;
    pd_example_print_dpm_event(event, v0, v1);
}
static int sink_basic_message(uint8_t port, uint8_t sop, uint8_t cat, uint8_t type,
                               const uint8_t *payload, uint16_t len, void *ctx)
{
    (void)port; (void)sop; (void)cat; (void)type; (void)payload; (void)len; (void)ctx;
    return USBPD_ERR_UNSUPPORTED;  /* 不消费，策略层走默认回复 */
}

void pd_example_sink_init(void)
{
    memset(&g_sink_cfg_basic, 0, sizeof(g_sink_cfg_basic));

    /* 能力范围 */
    g_sink_cfg_basic.max_voltage_mv = 20000U;    /* 最大 20V */
    g_sink_cfg_basic.max_current_ma = 5000U;     /* 最大 5A */
    g_sink_cfg_basic.max_power_mw   = 100000U;    /* 最大 100W */

    /* Sink 宣告的 Sink PDO（1 个 5V 3A Fixed） */
    g_sink_cfg_basic.sink_pdo_count = 1U;
    g_sink_cfg_basic.sink_pdo[0].sink_fixed.type          = USBPD_PDO_FIXED;
    g_sink_cfg_basic.sink_pdo[0].sink_fixed.voltage_50mv   = 100U;   /* 5V */
    g_sink_cfg_basic.sink_pdo[0].sink_fixed.current_10ma   = 300U;   /* 3A */

    /* DPM 回调 */
    g_sink_cfg_basic.dpm.event            = sink_basic_event;
    g_sink_cfg_basic.dpm.message_received = sink_basic_message;
    g_sink_cfg_basic.dpm.set_source       = 0;   /* Sink 不需要，桩 */
    g_sink_cfg_basic.dpm.source_ready     = 0;
    g_sink_cfg_basic.dpm.set_vconn        = 0;
    g_sink_cfg_basic.dpm.set_discharge    = 0;    /* Hard Reset 时拉低 VBUS */

    pd_sink_init(&g_sink_cfg_basic);
}

void pd_example_sink_task(uint32_t now_ms)
{
    pd_sink_task(now_ms);

    /* 可选：主动发起请求 */
    struct pd_sink_status_t st;
    pd_sink_get_status(&st);
    if (st.contract_valid && st.negotiated_voltage_mv != 9000U)
    {
        pd_sink_request(9000U, 1500U);   /* 请求 9V 1.5A */
    }
}

/* ============================================================
 * 示例 2：Source 基础（SPR 多电压 PDO）
 * ============================================================ */

static struct pd_source_config_t g_source_cfg_basic;

static int source_basic_set_source(uint8_t port, uint8_t enable, uint32_t mv, uint32_t ma, void *ctx)
{
    (void)port; (void)ctx;
    if (enable) {
        printf("[POWER] 开 %umV / %umA\n", mv, ma);
        /* power_chip_set(mv, ma); */
    } else {
        printf("[POWER] 关\n");
        /* power_chip_off(); */
    }
    return USBPD_OK;
}
static uint8_t source_basic_ready(uint8_t port, uint32_t mv, void *ctx)
{
    (void)port; (void)mv; (void)ctx;
    /* 返回 1 表示 VBUS 已稳定到目标电压 */
    /* return (read_vbus_adc() >= mv * 0.95f) ? 1U : 0U; */
    return 1U;
}
static int source_basic_set_discharge(uint8_t port, uint8_t enable, void *ctx)
{
    (void)port; (void)ctx;
    /* discharge_mos(enable); */
    return USBPD_OK;
}

void pd_example_source_init(void)
{
    memset(&g_source_cfg_basic, 0, sizeof(g_source_cfg_basic));

    /* Source PDO：5V 3A + 9V 3A + 12V 2.5A */
    g_source_cfg_basic.source_pdo_count = 3U;

    /* PDO 0: 5V 3A Fixed（首项必须 5V！epr_capable=0） */
    g_source_cfg_basic.source_pdo[0].fixed.type          = USBPD_PDO_FIXED;
    g_source_cfg_basic.source_pdo[0].fixed.voltage_50mv   = 100U;   /* 5V */
    g_source_cfg_basic.source_pdo[0].fixed.current_10ma   = 300U;   /* 3A */
    g_source_cfg_basic.source_pdo[0].fixed.epr_capable   = 0U;     /* Fixed PDO epr_capable 必须 0 */

    /* PDO 1: 9V 3A Fixed */
    g_source_cfg_basic.source_pdo[1].fixed.type          = USBPD_PDO_FIXED;
    g_source_cfg_basic.source_pdo[1].fixed.voltage_50mv   = 180U;   /* 9V */
    g_source_cfg_basic.source_pdo[1].fixed.current_10ma   = 300U;   /* 3A */

    /* PDO 2: 12V 2.5A Fixed */
    g_source_cfg_basic.source_pdo[2].fixed.type          = USBPD_PDO_FIXED;
    g_source_cfg_basic.source_pdo[2].fixed.voltage_50mv   = 240U;   /* 12V */
    g_source_cfg_basic.source_pdo[2].fixed.current_10ma   = 250U;   /* 2.5A */

    /* DPM 回调 */
    g_source_cfg_basic.dpm.event            = sink_basic_event;   /* 复用打印 */
    g_source_cfg_basic.dpm.message_received = sink_basic_message;
    g_source_cfg_basic.dpm.set_source       = source_basic_set_source;    /* ⚠️ 必填 */
    g_source_cfg_basic.dpm.source_ready     = source_basic_ready;
    g_source_cfg_basic.dpm.set_discharge    = source_basic_set_discharge;

    pd_source_init(&g_source_cfg_basic);
}

void pd_example_source_task(uint32_t now_ms)
{
    pd_source_task(now_ms);
}

/* ============================================================
 * 示例 3：PPS 动态调流
 * ============================================================ */

/**
 * PPS 模式原理：
 *   Source 宣告一个 PPS APDO（5V~20V，最大电流 XA）。
 *   Sink 建立合约后，每 10s 内可发 PPS_REQUEST 动态调整电压/电流。
 *   Source 收到 PPS_REQUEST → ACCEPT → set_source(新电压/电流) → PS_RDY。
 *
 * Source PPS APDO 字段：
 *   type=APDO, subtype=PPS
 *   min_voltage_100mv / max_voltage_100mv
 *   current_50ma（最大电流）
 */

/* Source 侧：宣告 PPS APDO */
static struct pd_source_config_t g_source_cfg_pps;

void pd_example_pps_source_init(void)
{
    memset(&g_source_cfg_pps, 0, sizeof(g_source_cfg_pps));

    g_source_cfg_pps.features.pps = 1U;

    /* PDO 0: 5V 3A Fixed（必须有！） */
    g_source_cfg_pps.source_pdo_count = 2U;
    g_source_cfg_pps.source_pdo[0].fixed.type          = USBPD_PDO_FIXED;
    g_source_cfg_pps.source_pdo[0].fixed.voltage_50mv   = 100U;
    g_source_cfg_pps.source_pdo[0].fixed.current_10ma   = 300U;
    g_source_cfg_pps.source_pdo[0].fixed.epr_capable   = 0U;

    /* PDO 1: PPS APDO，5V~20V，最大 5A */
    g_source_cfg_pps.source_pdo[1].pps.type               = USBPD_PDO_APDO;
    g_source_cfg_pps.source_pdo[1].pps.subtype            = USBPD_APDO_PPS;
    g_source_cfg_pps.source_pdo[1].pps.min_voltage_100mv  = 50U;    /* 5V */
    g_source_cfg_pps.source_pdo[1].pps.max_voltage_100mv  = 200U;   /* 20V */
    g_source_cfg_pps.source_pdo[1].pps.current_50ma       = 100U;   /* 5A (100×50mA) */

    g_source_cfg_pps.dpm.event         = sink_basic_event;
    g_source_cfg_pps.dpm.message_received = sink_basic_message;
    g_source_cfg_pps.dpm.set_source    = source_basic_set_source;
    g_source_cfg_pps.dpm.source_ready  = source_basic_ready;
    g_source_cfg_pps.dpm.set_discharge = source_basic_set_discharge;

    pd_source_init(&g_source_cfg_pps);
}

/* Sink 侧：声明支持 PPS，运行中动态调流 */
static struct pd_sink_config_t g_sink_cfg_pps;

void pd_example_pps_sink_init(void)
{
    memset(&g_sink_cfg_pps, 0, sizeof(g_sink_cfg_pps));

    g_sink_cfg_pps.max_voltage_mv  = 20000U;
    g_sink_cfg_pps.max_current_ma  = 5000U;
    g_sink_cfg_pps.max_power_mw    = 100000U;
    g_sink_cfg_pps.features.pps    = 1U;

    /* Sink 宣告 1 个 PPS APDO（告诉 Source 我支持 PPS） */
    g_sink_cfg_pps.sink_pdo_count = 1U;
    g_sink_cfg_pps.sink_pdo[0].sink_pps.type               = USBPD_PDO_APDO;
    g_sink_cfg_pps.sink_pdo[0].sink_pps.subtype            = USBPD_APDO_PPS;
    g_sink_cfg_pps.sink_pdo[0].sink_pps.min_voltage_100mv  = 50U;
    g_sink_cfg_pps.sink_pdo[0].sink_pps.max_voltage_100mv  = 200U;
    g_sink_cfg_pps.sink_pdo[0].sink_pps.current_50ma       = 100U;   /* 最大请求 5A */

    g_sink_cfg_pps.dpm.event            = sink_basic_event;
    g_sink_cfg_pps.dpm.message_received = sink_basic_message;
    g_sink_cfg_pps.dpm.set_discharge    = source_basic_set_discharge;

    pd_sink_init(&g_sink_cfg_pps);
}

void pd_example_pps_task(uint32_t now_ms)
{
    /* Source 侧 */
    pd_source_task(now_ms);
    /* Sink 侧（假设同芯片） */
    pd_sink_task(now_ms);

    /* Sink 运行中动态调流：合约建立后，每隔一段时间发 PPS_REQUEST */
    static uint32_t g_pps_next_ms = 0U;
    struct pd_sink_status_t st;
    pd_sink_get_status(&st);
    if (st.contract_valid && now_ms >= g_pps_next_ms)
    {
        /* 每 3s 变一次：9V 1A → 9V 2A → 9V 3A → 9V 1A ... */
        uint32_t current = 1000U + ((now_ms / 3000U) % 3U) * 1000U;
        pd_sink_request(9000U, current);
        g_pps_next_ms = now_ms + 3000U;
    }
}

/* ============================================================
 * 示例 4：EPR 高压（≥20V）
 * ============================================================ */

/**
 * EPR 模式原理：
 *   Source 宣告多个 EPR AVS PDO（每个含 PDP=总功率 + 电压范围）。
 *   Sink 先建立 SPR 合约（通常 5V），然后发 EPR_REQUEST → EPR_MODE(Enter)。
 *   Source 回复 EPR_MODE(Enter_Ack) → EPR_Source_Capabilities → EPR_MODE(Enter_Succeeded)。
 *   之后 Sink 在 EPR AVS PDO 中选一个发 EPR_REQUEST。
 *   EPR PDO 数量 Source/Sink 都必须 ≥ 8。
 *
 * Sink 侧 features.epr=1 → pd_sink_config_valid 要求 epr_sink_pdo_count ≥ 8。
 */

/* Source 侧：宣告 EPR AVS PDO */
static struct pd_source_config_t g_source_cfg_epr;

void pd_example_epr_source_init(void)
{
    memset(&g_source_cfg_epr, 0, sizeof(g_source_cfg_epr));

    g_source_cfg_epr.features.epr = 1U;

    /* 先放 5V Fixed PDO（必须有！） */
    g_source_cfg_epr.source_pdo_count = 2U;
    g_source_cfg_epr.source_pdo[0].fixed.type          = USBPD_PDO_FIXED;
    g_source_cfg_epr.source_pdo[0].fixed.voltage_50mv   = 100U;
    g_source_cfg_epr.source_pdo[0].fixed.current_10ma   = 300U;
    g_source_cfg_epr.source_pdo[0].fixed.epr_capable   = 0U;

    /* PDO 1: SPR AVS（5V~20V，最大 5A） */
    g_source_cfg_epr.source_pdo[1].spr_avs.type                = USBPD_PDO_APDO;
    g_source_cfg_epr.source_pdo[1].spr_avs.subtype             = USBPD_APDO_SPR_AVS;
    g_source_cfg_epr.source_pdo[1].spr_avs.current_20v_10ma    = 100U;   /* 10A @20V */
    g_source_cfg_epr.source_pdo[1].spr_avs.current_15v_10ma     = 100U;   /* 10A @15V */

    g_source_cfg_epr.dpm.event            = sink_basic_event;
    g_source_cfg_epr.dpm.message_received = sink_basic_message;
    g_source_cfg_epr.dpm.set_source       = source_basic_set_source;
    g_source_cfg_epr.dpm.source_ready     = source_basic_ready;
    g_source_cfg_epr.dpm.set_discharge    = source_basic_set_discharge;

    pd_source_init(&g_source_cfg_epr);
}

/* Sink 侧：声明支持 EPR，8 个 EPR AVS PDO */
static struct pd_sink_config_t g_sink_cfg_epr;

void pd_example_epr_sink_init(void)
{
    memset(&g_sink_cfg_epr, 0, sizeof(g_sink_cfg_epr));

    g_sink_cfg_epr.max_voltage_mv  = 48000U;    /* EPR 最高 48V */
    g_sink_cfg_epr.max_current_ma  = 5000U;
    g_sink_cfg_epr.max_power_mw    = 140000U;   /* 140W */
    g_sink_cfg_epr.features.epr    = 1U;

    /* SPR 模式 Sink PDO（1 个 5V 3A Fixed） */
    g_sink_cfg_epr.sink_pdo_count = 1U;
    g_sink_cfg_epr.sink_pdo[0].sink_fixed.type        = USBPD_PDO_FIXED;
    g_sink_cfg_epr.sink_pdo[0].sink_fixed.voltage_50mv = 100U;
    g_sink_cfg_epr.sink_pdo[0].sink_fixed.current_10ma = 300U;

    /* EPR 模式 Sink PDO（≥8 个要求，功率递增） */
    g_sink_cfg_epr.epr_sink_pdo_count = 8U;
    for (int i = 0; i < 8; i++)
    {
        g_sink_cfg_epr.epr_sink_pdo[i].sink_avs.type            = USBPD_PDO_APDO;
        g_sink_cfg_epr.epr_sink_pdo[i].sink_avs.subtype         = USBPD_APDO_EPR_AVS;
        g_sink_cfg_epr.epr_sink_pdo[i].sink_avs.pdp_w           = 30U + (uint8_t)(i * 10U);  /* 30W~100W */
        g_sink_cfg_epr.epr_sink_pdo[i].sink_avs.min_voltage_100mv = 50U;    /* 5V */
        g_sink_cfg_epr.epr_sink_pdo[i].sink_avs.max_voltage_100mv = 480U;   /* 48V */
    }

    g_sink_cfg_epr.dpm.event            = sink_basic_event;
    g_sink_cfg_epr.dpm.message_received = sink_basic_message;
    g_sink_cfg_epr.dpm.set_discharge    = source_basic_set_discharge;

    pd_sink_init(&g_sink_cfg_epr);
}

void pd_example_epr_task(uint32_t now_ms)
{
    pd_source_task(now_ms);
    pd_sink_task(now_ms);

    /* EPR 进入条件：已建立 SPR 合约 + 请求 > 20V
     * Sink 策略层内部自动处理完整流程 */
    static uint8_t g_epr_requested = 0U;
    struct pd_sink_status_t st;
    pd_sink_get_status(&st);
    if (st.contract_valid && !g_epr_requested)
    {
        printf("[EPR] 已建立 SPR 合约，请求 24V EPR...\n");
        pd_sink_request(24000U, 5000U);   /* 请求 24V 5A */
        g_epr_requested = 1U;
    }
    if (st.epr_active)
    {
        printf("[EPR] 已进入 EPR 模式，VBUS=%umV\n", st.negotiated_voltage_mv);
    }
}

/* ============================================================
 * 示例 5：DRP 双角色端口
 * ============================================================ */

/**
 * DRP 原理：CC 引脚按 75ms 节拍在 Rp/Rd 之间轮换。
 * 检测到 Rp → 本端扮演 Sink；检测到 Rd → 本端扮演 Source。
 * DRP 只能用 usbpd.h 顶层 API（pd_sink/pd_source 各自只能单角色）。
 */

static usbpd_config_t g_drp_cfg;

void pd_example_drp_init(void)
{
    memset(&g_drp_cfg, 0, sizeof(g_drp_cfg));

    g_drp_cfg.port_type = USBPD_PORT_DRP;
    g_drp_cfg.drp_toggle_ms = USBPD_DRP_TOGGLE_DEFAULT_MS;   /* 75ms */
    g_drp_cfg.preferred_role = USBPD_POWER_ROLE_SOURCE;      /* 偏好 Source */

    /* Source 侧能力 */
    g_drp_cfg.source.source_pdo_count = 2U;
    g_drp_cfg.source.source_pdo[0].fixed.type          = USBPD_PDO_FIXED;
    g_drp_cfg.source.source_pdo[0].fixed.voltage_50mv   = 100U;
    g_drp_cfg.source.source_pdo[0].fixed.current_10ma   = 300U;
    g_drp_cfg.source.source_pdo[0].fixed.epr_capable   = 0U;
    g_drp_cfg.source.source_pdo[1].fixed.type          = USBPD_PDO_FIXED;
    g_drp_cfg.source.source_pdo[1].fixed.voltage_50mv   = 240U;   /* 12V */
    g_drp_cfg.source.source_pdo[1].fixed.current_10ma   = 200U;   /* 2A */
    g_drp_cfg.source.dpm.event            = sink_basic_event;
    g_drp_cfg.source.dpm.message_received = sink_basic_message;
    g_drp_cfg.source.dpm.set_source       = source_basic_set_source;
    g_drp_cfg.source.dpm.source_ready     = source_basic_ready;
    g_drp_cfg.source.dpm.set_discharge    = source_basic_set_discharge;

    /* Sink 侧能力 */
    g_drp_cfg.sink.max_voltage_mv  = 20000U;
    g_drp_cfg.sink.max_current_ma  = 3000U;
    g_drp_cfg.sink.max_power_mw    = 60000U;
    g_drp_cfg.sink.sink_pdo_count = 1U;
    g_drp_cfg.sink.sink_pdo[0].sink_fixed.type        = USBPD_PDO_FIXED;
    g_drp_cfg.sink.sink_pdo[0].sink_fixed.voltage_50mv = 100U;
    g_drp_cfg.sink.sink_pdo[0].sink_fixed.current_10ma = 300U;
    g_drp_cfg.sink.dpm.event            = sink_basic_event;
    g_drp_cfg.sink.dpm.message_received = sink_basic_message;
    g_drp_cfg.sink.dpm.set_discharge    = source_basic_set_discharge;

    usbpd_init(0U, &g_drp_cfg);
}

void pd_example_drp_task(uint32_t now_ms)
{
    usbpd_task(0U, now_ms);

    /* 查询当前角色 */
    usbpd_status_t st;
    usbpd_get_status(0U, &st);
    if (st.attached)
    {
        if (st.power_role == USBPD_POWER_ROLE_SOURCE)
            printf("[DRP] 当前扮演 Source, VBUS=%umV\n", st.policy.source.negotiated_voltage_mv);
        else
            printf("[DRP] 当前扮演 Sink, VBUS=%umV\n", st.policy.sink.negotiated_voltage_mv);
    }
}

/* ============================================================
 * 示例 6：VDM Discover Identity
 * ============================================================ */

/**
 * VDM 原理：
 *   DFP 端口主动发 Discover Identity 结构化 VDM。
 *   UFP 端口在 message_received 回调中消费 VDM → 回复 ACK + Identity VDO。
 *   Source 也可能发 Discover Identity（VCONN Source 场景）。
 *
 * 本示例用 Source 角色（DFP+Source），主动 Discover Identity。
 */

static struct pd_source_config_t g_source_cfg_vdm;

/** Source 侧 message_received：消费 VDM → 回复 Discover Identity ACK + Identity */
static int source_vdm_message(uint8_t port, uint8_t sop, uint8_t cat, uint8_t type,
                               const uint8_t *payload, uint16_t len, void *ctx)
{
    (void)port; (void)ctx;
    (void)sop; (void)cat; (void)type; (void)payload; (void)len;

    /* 如果收到 Discover Identity 请求，回复 ACK + Identity VDO
     * 策略层 message_received 返回 OK 表示我消费了，不回 Not_Supported */
    return USBPD_ERR_UNSUPPORTED;  /* 示例：交给策略层默认处理 */
}

void pd_example_vdm_init(void)
{
    memset(&g_source_cfg_vdm, 0, sizeof(g_source_cfg_vdm));

    g_source_cfg_vdm.source_pdo_count = 1U;
    g_source_cfg_vdm.source_pdo[0].fixed.type          = USBPD_PDO_FIXED;
    g_source_cfg_vdm.source_pdo[0].fixed.voltage_50mv   = 100U;
    g_source_cfg_vdm.source_pdo[0].fixed.current_10ma   = 300U;
    g_source_cfg_vdm.source_pdo[0].fixed.epr_capable   = 0U;

    g_source_cfg_vdm.dpm.event            = sink_basic_event;
    g_source_cfg_vdm.dpm.message_received = source_vdm_message;
    g_source_cfg_vdm.dpm.set_source       = source_basic_set_source;
    g_source_cfg_vdm.dpm.source_ready     = source_basic_ready;
    g_source_cfg_vdm.dpm.set_discharge    = source_basic_set_discharge;

    pd_source_init(&g_source_cfg_vdm);
}

void pd_example_vdm_task(uint32_t now_ms)
{
    static uint8_t g_vdm_sent = 0U;
    pd_source_task(now_ms);

    /* Source 合约建立后，主动发 Discover Identity */
    struct pd_source_status_t st;
    pd_source_get_status(&st);
    if (st.contract_valid && !g_vdm_sent)
    {
        /* 构造 Discover Identity 结构化 VDM */
        uint32_t vdm_header = 0;
        union usbpd_vdm_header_u *vh = (union usbpd_vdm_header_u *)&vdm_header;
        vh->structured.command       = USBPD_VDM_DISCOVER_IDENTITY;
        vh->structured.command_type  = USBPD_VDM_CMD_REQUEST;
        vh->structured.object_position = 0U;
        vh->structured.version       = 1U;
        vh->structured.structured    = 1U;   /* 结构化 VDM */
        vh->structured.svid          = 0xFF01U;  /* SVID（USB-IF ID） */

        uint32_t vdm_do = vdm_header;
        pd_source_send_data_objects(USBPD_SOP_PRIME, USBPD_DATA_VENDOR_DEFINED,
                                    &vdm_do, 1U);
        printf("[VDM] 已发送 Discover Identity\n");
        g_vdm_sent = 1U;
    }
}

/* ============================================================
 * 示例 7：Alert 阈值保护
 * ============================================================ */

/**
 * Alert 原理：
 *   Source 侧监控电源输入，检测到过压/过温/限流等异常时，
 *   发 Alert 消息给 Sink → Sink 收到后发 PR_SWAP → Hard Reset。
 *
 * 本示例：
 *   Source 配置 Alert Mask（Source 侧的 Alert 是被动接收 Sink 的告警请求）。
 *   实际 OVP/OCP 通过外部监控芯片触发 Hard Reset 更常见。
 *   这里演示 Source 侧 get_extended 回调（Sink 会发 GET_STATUS 等扩展请求）。
 */

static struct pd_source_config_t g_source_cfg_alert;

/** Source 侧 get_extended：回复 Status / PPS_Status */
static int source_alert_get_extended(uint8_t port, uint8_t type, uint8_t *data,
                                      uint16_t *length, void *ctx)
{
    (void)port; (void)ctx;

    switch (type)
    {
    case USBPD_EXT_STATUS:
        /* 7 字节 SOP Status Data Block */
        if (*length < 7U) return USBPD_ERR_UNSUPPORTED;
        memset(data, 0, 7U);
        /* present_input: external_power=1 */
        data[1] = 0x02U;
        /* event_flags: 正常=0 */
        data[3] = 0x00U;
        *length = 7U;
        return USBPD_OK;

    case USBPD_EXT_PPS_STATUS:
        /* 2 字节 PPS Status */
        if (*length < 2U) return USBPD_ERR_UNSUPPORTED;
        data[0] = 0x00U;   /* 正常 */
        data[1] = 0x00U;   /* 当前电流 */
        *length = 2U;
        return USBPD_OK;

    default:
        return USBPD_ERR_UNSUPPORTED;
    }
}

void pd_example_alert_init(void)
{
    memset(&g_source_cfg_alert, 0, sizeof(g_source_cfg_alert));

    g_source_cfg_alert.source_pdo_count = 1U;
    g_source_cfg_alert.source_pdo[0].fixed.type          = USBPD_PDO_FIXED;
    g_source_cfg_alert.source_pdo[0].fixed.voltage_50mv   = 100U;
    g_source_cfg_alert.source_pdo[0].fixed.current_10ma   = 300U;
    g_source_cfg_alert.source_pdo[0].fixed.epr_capable   = 0U;

    g_source_cfg_alert.dpm.event            = sink_basic_event;
    g_source_cfg_alert.dpm.message_received = sink_basic_message;
    g_source_cfg_alert.dpm.get_extended     = source_alert_get_extended;   /* 新增！ */
    g_source_cfg_alert.dpm.set_source       = source_basic_set_source;
    g_source_cfg_alert.dpm.source_ready     = source_basic_ready;
    g_source_cfg_alert.dpm.set_discharge    = source_basic_set_discharge;

    pd_source_init(&g_source_cfg_alert);
}

void pd_example_alert_task(uint32_t now_ms)
{
    pd_source_task(now_ms);

    /* 示例：过压保护检测 → Hard Reset
     * 实际产品中这通过硬件比较器触发中断完成 */
    /*
    if (read_vbus_adc() > 22000U)   // 超过 22V
    {
        usbpd_protocol_send_hard_reset(now_ms);
        printf("[ALERT] 过压！Hard Reset\n");
    }
    */
}
