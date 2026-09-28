/**
* @file pd_app.h
* @brief USB PD 协议栈完整示例接口声明
*
* 所属模块：USB PD 驱动示例（覆盖协议栈全部支持的角色与特性）。
* 对应实现：pd_app.c；依赖：usbpd.h + usbpd_dpm.h + pd_sink.h + pd_source.h。
*
* 本示例覆盖的全部特性：
*   1. Sink 基础示例（SPR，固定 PDO）
*   2. Source 基础示例（SPR，多电压 PDO）
*   3. PPS 动态调流（Sink 请求 Source PPS APDO，运行中动态变流）
*   4. EPR 高压（≥20V，完整 EPR_CAPS → EPR_ENTER → EPR_REQUEST → EPR_MODE 流程）
*   5. DRP 双角色端口（rp/rd 自动切换）
*   6. VDM Discover Identity（结构化 VDM 请求 + 响应）
*   7. Alert 阈值保护（Source OVP/OCP/OTP → Alert 消息 → Hard Reset）
*   8. DPM 完整回调（event / message_received / get_extended / set_source / source_ready / set_discharge）
*/
#ifndef PD_APP_H
#define PD_APP_H

#include "usbpd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ===== 示例 1：Sink 基础示例（SPR 5V 固定 PDO） ===== */

/**
 * @brief  Sink SPR 示例初始化
 *
 * 配置 1 个 5V 3A Fixed Sink PDO，最大接受 20V/5A/100W。
 */
void pd_example_sink_init(void);

/**
 * @brief  Sink SPR 示例主循环（毫秒节拍）
 */
void pd_example_sink_task(uint32_t now_ms);

/* ===== 示例 2：Source 基础示例（SPR 多电压 PDO） ===== */

/**
 * @brief  Source SPR 示例初始化
 *
 * 配置 3 个 Source PDO：5V 3A + 9V 3A + 12V 2.5A。
 */
void pd_example_source_init(void);

/**
 * @brief  Source SPR 示例主循环（毫秒节拍）
 */
void pd_example_source_task(uint32_t now_ms);

/* ===== 示例 3：PPS 动态调流 ===== */

/**
 * @brief  PPS 示例初始化（Source 侧宣告 PPS APDO）
 */
void pd_example_pps_source_init(void);

/**
 * @brief  PPS 示例初始化（Sink 侧声明支持 PPS，运行中动态调流）
 */
void pd_example_pps_sink_init(void);

/**
 * @brief  PPS 示例主循环（毫秒节拍，两端都要调）
 */
void pd_example_pps_task(uint32_t now_ms);

/* ===== 示例 4：EPR 高压 ===== */

/**
 * @brief  EPR 示例初始化（Source 侧宣告 EPR AVS PDO）
 */
void pd_example_epr_source_init(void);

/**
 * @brief  EPR 示例初始化（Sink 侧声明支持 EPR，请求高压）
 */
void pd_example_epr_sink_init(void);

/**
 * @brief  EPR 示例主循环（毫秒节拍，两端都要调）
 */
void pd_example_epr_task(uint32_t now_ms);

/* ===== 示例 5：DRP 双角色端口 ===== */

/**
 * @brief  DRP 示例初始化（port_type=DRP，自动轮换 Source/Sink）
 */
void pd_example_drp_init(void);

/**
 * @brief  DRP 示例主循环（毫秒节拍）
 */
void pd_example_drp_task(uint32_t now_ms);

/* ===== 示例 6：VDM Discover Identity ===== */

/**
 * @brief  VDM 示例初始化（DFP 角色，可主动发 Discover Identity）
 */
void pd_example_vdm_init(void);

/**
 * @brief  VDM 示例主循环（毫秒节拍）
 */
void pd_example_vdm_task(uint32_t now_ms);

/* ===== 示例 7：Alert 阈值保护 ===== */

/**
 * @brief  Alert 示例初始化（Source 侧配置 OVP/OCP 阈值）
 */
void pd_example_alert_init(void);

/**
 * @brief  Alert 示例主循环（毫秒节拍）
 */
void pd_example_alert_task(uint32_t now_ms);

/* ===== 通用 ===== */

/**
 * @brief  打印全部示例共用的 DPM 事件处理（可直接复用）
 *
 * 作为 message_received / event / get_extended / set_source 等回调的参考实现。
 */
void pd_example_print_dpm_event(uint8_t event, uint32_t v0, uint32_t v1);

#ifdef __cplusplus
}
#endif

#endif /* PD_APP_H */
