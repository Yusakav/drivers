/**
 * @file pd_app.c
 * @author YusakaV (YusakaVivy@gmail.com)
 * @brief USB PD 应用层驱动 — Type-C 连接检测、CC 去抖、PD 协商调度与 VBUS 监控
 * @version 1.0
 * @date 2026-06-13
 *
 * @copyright Copyright (c) 2026
 *
 * @note  依赖底层驱动接口:
 *    - usb_pd_init(int port)        → 初始化 PD 端口
 *    - usb_pd_process(int port)     → PD 状态机循环
 *    - usb_pd_phy_get_cc_state()    → 读取 CC 电平
 *    - usb_pd_phy_set_pull()        → 设置 CC 上下拉
 *    - usb_pd_phy_set_sel()         → 设置 CC 极性
 *    - usb_phy_get_vbus()           → 读取 VBUS 电压
 *    - usb_pd_get_state(int port)   → 获取底层 PD 协议状态机当前状态
 *      (需在 usb_pd_protocol.c 中实现，声明见下方 extern)
 *
 */

#include "ch32x035.h"
#include "timer.h"
#include "pd_app.h"
#include "string.h"
#include "usb_pd_phy.h"

#define LOG_LEVEL LOG_LVL_DEBUG
#define LOG_TAG  "PD APP"
#include "logger.h"

/* ==================== 外部依赖声明 ==================== */

/**
 * @brief 获取底层 PD 协议状态机当前状态
 *
 * 若 usb_pd_protocol.c 中的 struct usb_pd_port pd[] 为 static，
 * 需在 usb_pd_protocol.c 中添加此函数的实现：
 *
 *   enum usbpd_states_e usb_pd_get_state(int port) {
 *       return pd[port].task_state;
 *   }
 *
 * 并在 usb_pd_protocol.h 中声明。
 */
extern enum usbpd_states_e usb_pd_get_state(int port);

/**
 * @brief 获取 PD 协商结果
 *
 * 返回协商后的电压(mV)、电流(mA)、功率(mW)和 PDO 索引。
 * 声明于 usb_pd_protocol.h，由底层协议层实现。
 */
extern int usb_pd_get_negotiation(int port, uint32_t *voltage_mv, uint32_t *current_ma, uint32_t *power_mw, uint8_t *pdo_idx);

/**
 * @brief 读取 VBUS 电压
 *
 * 声明于 usb_pd_phy.h，由底层 PHY 驱动实现
 */
extern int usb_phy_get_vbus(int *mv);

/* ==================== 本地配置宏 ==================== */

/** VBUS 安全跌落阈值 (mV)，低于此值认为供电断开 */
#define CONFIG_USB_PDVBUS_VSAFE_5V   4500

/** CC 轮询间隔 (ms)，用于 DISCONNECTED 状态重试 */
#define PD_APP_CC_POLL_MS            200

/** 状态打印周期 (ms) */
#define PD_APP_STATUS_PRINT_MS       5000

/* ==================== 全局应用实例 ==================== */

struct pd_app_t pd_app;

/* ==================== 状态机名称表 (调试用) ==================== */

static const char *const pd_app_state_names[] = {
    "PD_APP_STATE_INIT",
    "PD_APP_STATE_DISCONNECTED",
    "PD_APP_STATE_DEBOUNCE",
    "PD_APP_STATE_ATTACHED",
    "PD_APP_STATE_NEGOTIATING",
    "PD_APP_STATE_READY",
    "PD_APP_STATE_IDLE",
    "PD_APP_STATE_TIMEOUT",
    "PD_APP_STATE_ERROR",
};

/* ==================== 运行期变量 ==================== */

static uint32_t last_print_ms;

/* ==================== 内部辅助函数 ==================== */

static void set_state_machine_timeout(uint64_t timeout, enum pd_app_states_e timeout_state) {
    pd_app.timeout       = timeout;
    pd_app.timeout_state = timeout_state;
}

/**
 * @brief 切换状态机状态，自动记录耗时
 */
static void set_state_machine_state(enum pd_app_states_e next_state)
{
    enum pd_app_states_e last_state = pd_app.task_state;

    if (last_state == next_state)
        return;

    /* 记录上一阶段的耗时 */
    if (last_state < PD_APP_STATE_COUNT) {
        if (pd_app.stages[last_state].enter_ms != 0) {
            uint32_t now_ms = get_time().val;
            pd_app.stages[last_state].elapsed_ms = now_ms - pd_app.stages[last_state].enter_ms;
        }
    }

    /* 清除超时计时器 */
    set_state_machine_timeout(0, 0);

    /* 记录新阶段的进入时间 */
    pd_app.task_state = next_state;
    if (next_state < PD_APP_STATE_COUNT) {
        pd_app.stages[next_state].enter_ms  = get_time().val;
        pd_app.stages[next_state].elapsed_ms = 0;
    }

    pd_app.sub_step = 0;

    LOG_I("%s -> %s", pd_app_state_names[last_state], pd_app_state_names[next_state]);

    if (pd_app.notify_handler) {
        pd_app.notify_handler((uint8_t)next_state, NULL);
    }
}

/**
 * @brief 记录错误并跳转错误状态
 */
static void enter_error_state(const char *reason)
{
    LOG_E("错误: %s", reason);
    set_state_machine_state(PD_APP_STATE_ERROR);
}

/* ==================== 各阶段处理函数 ==================== */

/**
 * @brief 阶段 0: 初始化 (INIT)
 *
 * 步骤:
 *   1. 调用 usb_pd_init() 初始化底层 PD 端口
 *   2. 检查返回值，成功 → DISCONNECTED，失败 → ERROR
 */
static void process_state_init(void)
{
    pd_app.sub_step++;

    switch (pd_app.sub_step) {

    case 1:
        LOG_I("开始初始化 USB PD 端口 %d...", pd_app.port);
        pd_app.init_ret = usb_pd_init(pd_app.port);
        break;

    case 2:
        if (pd_app.init_ret != 0) {
            enter_error_state("usb_pd_init 失败");
            return;
        }
        LOG_I("USB PD 端口 %d 初始化成功, 进入等待连接状态", pd_app.port);
        set_state_machine_state(PD_APP_STATE_DISCONNECTED);
        break;
    }
}

/**
 * @brief 阶段 1: 等待 Type-C 连接 (DISCONNECTED)
 *
 * 步骤:
 *   1. 周期性调用 usb_pd_process() 驱动底层状态机
 *   2. 读取 CC1/CC2 状态
 *   3. 若任一 CC 非 OPEN → 记录极性 → 跳转 DEBOUNCE
 *   4. 未检测到连接 → 设置 200ms 超时后重试
 */
static void process_state_disconnected(void)
{
    enum usbpd_cc_voltage_status_e cc1, cc2;
    uint8_t cc_polarity = 0;

    pd_app.sub_step++;

    switch (pd_app.sub_step) {

    case 1:
        /* 驱动底层 PD 状态机 */
        usb_pd_process(pd_app.port);
        break;

    case 2:
        /* 读取 CC 引脚状态 */
        usb_pd_phy_get_cc_state(pd_app.port, &cc1, &cc2);
        pd_app.cc1_state = (uint8_t)cc1;
        pd_app.cc2_state = (uint8_t)cc2;

        /* 检查是否有连接 */
        if (cc1 != TYPEC_CC_VOLT_OPEN || cc2 != TYPEC_CC_VOLT_OPEN) {

            /* 选择非 OPEN 的 CC 作为有效通信线 */
            if (cc1 != TYPEC_CC_VOLT_OPEN) {
                cc_polarity = 1;  /* CC1 */
            } else {
                cc_polarity = 2;  /* CC2 */
            }
            pd_app.cc_polarity = cc_polarity;

            LOG_I("检测到 Type-C 连接: CC1=0x%02X, CC2=0x%02X, 极性=CC%d",
                  cc1, cc2, cc_polarity);

            set_state_machine_state(PD_APP_STATE_DEBOUNCE);
            return;
        }
        break;

    case 3:
        /* 未检测到连接，设置轮询超时 */
        set_state_machine_timeout(
            get_time().val + PD_APP_CC_POLL_MS,
            PD_APP_STATE_DISCONNECTED);
        break;
    }
}

/**
 * @brief 阶段 2: CC 去抖 (DEBOUNCE)
 *
 * 步骤:
 *   1. 重新读取 CC 状态，确认稳定
 *   2. 设置 CC 极性 → 跳转 ATTACHED
 *   3. 超时保护: USBPD_T_CC_DEBOUNCE
 */
static void process_state_debounce(void)
{
    enum usbpd_cc_voltage_status_e cc1, cc2;

    pd_app.sub_step++;

    switch (pd_app.sub_step) {

    case 1:
        /* 重新读取 CC 状态进行去抖确认 */
        usb_pd_phy_get_cc_state(pd_app.port, &cc1, &cc2);
        pd_app.cc1_state = (uint8_t)cc1;
        pd_app.cc2_state = (uint8_t)cc2;

        /* 确认连接仍然存在 */
        if (cc1 == TYPEC_CC_VOLT_OPEN && cc2 == TYPEC_CC_VOLT_OPEN) {
            LOG_I("去抖失败: CC 连接已消失, 回退到 DISCONNECTED");
            set_state_machine_state(PD_APP_STATE_DISCONNECTED);
            return;
        }

        /* 更新极性 */
        if (cc1 != TYPEC_CC_VOLT_OPEN) {
            pd_app.cc_polarity = 1;
        } else {
            pd_app.cc_polarity = 2;
        }

        LOG_I("去抖通过: CC%d 稳定, 设置极性并附着", pd_app.cc_polarity);
        break;

    case 2:
        /* 设置 CC 极性 */
        usb_pd_phy_set_sel(pd_app.cc_polarity);

        LOG_I("CC 极性已设置为 CC%d", pd_app.cc_polarity);
        set_state_machine_state(PD_APP_STATE_ATTACHED);
        break;
    }
}

/**
 * @brief 阶段 3: 附着后等待 PD 协商 (ATTACHED)
 *
 * 步骤:
 *   1. 周期性调用 usb_pd_process() 驱动底层状态机
 *   2. 读取 VBUS 电压
 *   3. 检查底层状态机是否进入 SNK_READY / SRC_READY
 *   4. 超时保护: USBPD_T_SINK_WAIT_CAP
 */
static void process_state_attached(void)
{
    enum usbpd_states_e pd_state;
    int vbus_mv = 0;

    pd_app.sub_step++;

    switch (pd_app.sub_step) {

    case 1:
        /* 驱动底层 PD 状态机 */
        usb_pd_process(pd_app.port);
        break;

    case 2:
        /* 读取 VBUS 电压 */
        if (usb_phy_get_vbus(&vbus_mv) == 0) {
            pd_app.vbus_mv = vbus_mv;
        }
        break;

    case 3:
        /* 检查底层 PD 状态机是否进入就绪状态 */
        pd_state = usb_pd_get_state(pd_app.port);

        if (pd_state == PD_STATE_SNK_READY || pd_state == PD_STATE_SRC_READY) {
            LOG_I("PD 协商完成: 底层状态机进入 SNK_READY/SRC_READY, VBUS=%dmV",
                  pd_app.vbus_mv);
            set_state_machine_state(PD_APP_STATE_READY);
            return;
        }
        break;

    case 4:
        /* 设置协商等待超时 */
        set_state_machine_timeout(
            get_time().val + USBPD_T_SINK_WAIT_CAP,
            PD_APP_STATE_TIMEOUT);
        break;
    }
}

/**
 * @brief 阶段 3.5: PD 协商中 (NEGOTIATING)
 *
 * 步骤:
 *   1. 周期调用 usb_pd_process() 驱动底层状态机
 *   2. 检查底层状态变化
 *   3. 检测到 READY 则跳转 PD_APP_STATE_READY
 */
static void process_state_negotiating(void)
{
    enum usbpd_states_e pd_state;

    pd_app.sub_step++;

    switch (pd_app.sub_step) {

    case 1:
        /* 驱动底层 PD 状态机 */
        usb_pd_process(pd_app.port);
        break;

    case 2:
        /* 检查底层状态机是否进入就绪状态 */
        pd_state = usb_pd_get_state(pd_app.port);

        if (pd_state == PD_STATE_SNK_READY || pd_state == PD_STATE_SRC_READY) {
            LOG_I("PD 协商完成: 底层状态机进入就绪状态, 跳转 READY");
            set_state_machine_state(PD_APP_STATE_READY);
            return;
        }
        break;

    case 3:
        /* 设置轮询超时 */
        set_state_machine_timeout(
            get_time().val + 10,
            PD_APP_STATE_NEGOTIATING);
        break;
    }
}

/**
 * @brief 阶段 4: 供电就绪 (READY)
 *
 * 步骤:
 *   1. 周期性调用 usb_pd_process()
 *   2. 周期性打印 VBUS 电压和协商结果
 *   3. 检测 VBUS 是否跌落 (< VSAFE_5V)，若是 → 回退 DISCONNECTED
 */
static void process_state_ready(void)
{
    int vbus_mv = 0;
    uint32_t now_ms;

    pd_app.sub_step++;

    switch (pd_app.sub_step) {

    case 1:
        /* 驱动底层 PD 状态机 */
        usb_pd_process(pd_app.port);
        break;

    case 2:
        /* 读取 VBUS 电压 */
        if (usb_phy_get_vbus(&vbus_mv) == 0) {
            pd_app.vbus_mv = vbus_mv;
        }
        break;

    case 3:
        /* 填充协商结果 */
        {
            uint32_t voltage_mv = 0, current_ma = 0, power_mw = 0;
            uint8_t pdo_idx = 0;
            int neg_ret = usb_pd_get_negotiation(pd_app.port,
                &voltage_mv, &current_ma, &power_mw, &pdo_idx);
            if (neg_ret == 0) {
                pd_app.negotiation.negotiated = 1;
                pd_app.negotiation.voltage_mv = voltage_mv;
                pd_app.negotiation.current_ma = current_ma;
                pd_app.negotiation.power_mw = power_mw;
                pd_app.negotiation.pdo_index = pdo_idx;
            }
        }

        /* 周期性打印状态 */
        now_ms = get_time().val;
        if (now_ms - last_print_ms > PD_APP_STATUS_PRINT_MS) {
            LOG_I("供电就绪: VBUS=%dmV | 协商=%s | 电压=%dmV 电流=%dmA 功率=%dmW",
                  pd_app.vbus_mv,
                  pd_app.negotiation.negotiated ? "是" : "否",
                  pd_app.negotiation.voltage_mv,
                  pd_app.negotiation.current_ma,
                  pd_app.negotiation.power_mw);
            last_print_ms = now_ms;
        }
        break;

    case 4:
        /* VBUS 跌落检测 */
        if (pd_app.vbus_mv > 0 && pd_app.vbus_mv < CONFIG_USB_PDVBUS_VSAFE_5V) {
            LOG_I("VBUS 跌落 (%dmV < %dmV), 清理协商结果, 回退到 DISCONNECTED",
                  pd_app.vbus_mv, CONFIG_USB_PDVBUS_VSAFE_5V);
            memset(&pd_app.negotiation, 0, sizeof(pd_app.negotiation));
            set_state_machine_state(PD_APP_STATE_DISCONNECTED);
            return;
        }
        break;

    case 5:
        /* 继续监控 */
        set_state_machine_timeout(
            get_time().val + 100,
            PD_APP_STATE_READY);
        break;
    }
}

/* ==================== 公开接口 ==================== */

/**
 * @brief 初始化 PD 应用
 *
 * 设置初始状态为 INIT，状态机在 process() 中驱动
 *
 * @param port PD 端口号 (通常为 0)
 */
void pd_app_init(int port)
{
    memset(&pd_app, 0, sizeof(pd_app));
    pd_app.task_state = PD_APP_STATE_INIT;
    pd_app.last_state = PD_APP_STATE_INIT;
    pd_app.port        = port;
    last_print_ms      = get_time().val;
    LOG_I("PD 应用层初始化完成，端口 %d，状态机启动", port);
}

/**
 * @brief 处理 PD 应用 (需在主循环中周期性调用)
 *
 * 状态机驱动函数，按序完成 Type-C 连接检测 → CC 去抖 → PD 协商调度 → VBUS 监控
 */
void pd_app_process(void)
{
    enum pd_app_states_e this_state;
    union  timestamp_u      now;

    this_state = pd_app.task_state;

    /* 状态同步 */
    if (pd_app.last_state != this_state) {
        /* 状态已在上层 set 函数中记录日志，此处仅同步 */
    }
    pd_app.last_state = this_state;

    /* 阶段处理 */
    switch (this_state) {

    case PD_APP_STATE_INIT:
        process_state_init();
        break;

    case PD_APP_STATE_DISCONNECTED:
        process_state_disconnected();
        break;

    case PD_APP_STATE_DEBOUNCE:
        process_state_debounce();
        break;

    case PD_APP_STATE_ATTACHED:
        process_state_attached();
        break;

    case PD_APP_STATE_NEGOTIATING:
        process_state_negotiating();
        break;

    case PD_APP_STATE_READY:
        process_state_ready();
        break;

    case PD_APP_STATE_IDLE:
        if (pd_app.last_state != PD_APP_STATE_IDLE) {
            LOG_I("进入空闲状态");
        }
        break;

    case PD_APP_STATE_TIMEOUT:
        if (pd_app.last_state != PD_APP_STATE_TIMEOUT) {
            LOG_I("超时: 从 %s 超时",
                  pd_app_state_names[pd_app.last_state]);
        }
        break;

    case PD_APP_STATE_ERROR:
        if (pd_app.last_state != PD_APP_STATE_ERROR) {
            LOG_E("致命错误，状态机停止");
        }
        break;

    default:
        break;
    }

    /* 超时监控 */
    if (pd_app.timeout) {
        now = get_time();
        if (now.val >= pd_app.timeout) {
            set_state_machine_state(pd_app.timeout_state);
        }
    }
}
