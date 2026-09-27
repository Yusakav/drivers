# USB PD R3.2 CH32X035 驱动说明

本文档说明 `USB_PD/` 当前驱动的结构、接口、消息定义、策略状态机、诊断缓冲、CH32X035 适配和验证方法。驱动目标是 USB Power Delivery R3.2 V1.1，当前以 CH32X035 为实机后端，公开 API 使用统一的 `usbpd_*` 入口，当前只支持 `port == 0`。

## 1. 范围与边界

当前代码支持三种端口类型：

| 端口类型 | 配置值 | 当前行为 |
| --- | --- | --- |
| Sink | `USBPD_PORT_SINK` | Type-C 附着、Source Capabilities 解析、SPR 合同、PPS、SPR AVS、EPR Sink 流程、扩展消息和复位恢复 |
| Source | `USBPD_PORT_SOURCE` | SPR Source 供电策略、Fixed/Variable/Battery/PPS/SPR AVS 请求评估、PS_RDY 时序、部分扩展消息响应 |
| DRP | `USBPD_PORT_DRP` | Rd/Rp 周期切换，附着后分派到 Sink 或 Source 策略 |

当前代码不实现以下能力：

| 能力 | 当前处理 |
| --- | --- |
| 多端口 | `port != 0` 返回 `USBPD_ERR_UNSUPPORTED` |
| CH32L103 后端 | 未纳入本轮代码，只保留未来用同名 PHY 函数替换的边界 |
| PR_Swap、DR_Swap、VCONN_Swap | 规范拒绝路径，不切换角色 |
| FRS | 不实现 |
| EPR Source | 不发布 EPR Source 能力，不进入 EPR Source 供电 |
| VCONN Source | 不主动实现线缆供电和线缆发现完整闭环 |

所有协议定义集中在 `inc/usbpd_def.h`，包括控制消息、数据消息、扩展消息、PDO、RDO、VDM、EPR 对象、扩展数据块、错误码、定时常量和位域联合体。

## 2. 目录结构

```text
USB_PD/
  inc/
    usbpd.h                 公开 API
    usbpd_def.h             协议枚举、位域、PDO/RDO/VDM/EPR/扩展数据块
    usbpd_types.h           兼容旧引用，转发包含 usbpd_def.h
    usbpd_protocol.h        协议层接口
    pd_sink.h               Sink 策略接口
    pd_source.h             Source 策略接口
    usbpd_dpm.h             Device Policy Manager 回调
    usbpd_message.h         观测与可读日志接口
  common/message_buffer/
    message_buffer.h/.c     通用固定容量环形缓冲区
  platform/ch32x035/
    usbpd_phy_ch32x035.h/.c CH32X035 PHY、寄存器、DMA、CC、IRQ、GoodCRC
  src/
    usbpd.c                 公开 API、端口分派、DRP 切换
    usbpd_protocol.c        Message ID、GoodCRC、重传、分块、复位
    pd_sink.c               Sink 策略状态机
    pd_source.c             Source 策略状态机
    usbpd_frame.c           PD 帧编解码和校验
    usbpd_message.c         诊断记录与可读格式化
  tests/
    host/                   CMake/GCC 主机测试与 fake PHY
  doc/
    CH32X035_VALIDATION.md  实机验证清单
    usbpd_sink_logic.md     Sink 逻辑简述
```

模块边界的原则是：只有 `platform/ch32x035/usbpd_phy_ch32x035.c` 可以直接包含 CH32X035 SDK 寄存器头文件。`usbpd_protocol.c`、`pd_sink.c`、`pd_source.c`、消息模块和测试代码不直接依赖芯片 SDK。

## 3. 分层关系

调用方向如下：

```text
应用
  |
  v
usbpd_* 公开端口 API
  |
  +-- DRP 端口切换与状态合并
  |
  +-- pd_sink.c / pd_source.c
        |
        v
     usbpd_protocol.c
        |
        v
     usbpd_phy_* 直接函数
        |
        v
     CH32X035 USBPD 外设、DMA、CC、VBUS、IRQ
```

协议层直接调用 `usbpd_phy_*`，没有 `usbpd_platform_ops_t`，也没有函数指针分发。未来如果加入 CH32L103，应该提供同名 `usbpd_phy_*` 函数并替换构建文件，而不是改 Sink、Source 或协议层。

## 4. 公开 API

头文件：`inc/usbpd.h`

```c
int usbpd_init(uint8_t port, const usbpd_config_t *config);
void usbpd_task(uint8_t port, uint32_t now_ms);
int usbpd_request(uint8_t port, uint32_t voltage_mv, uint32_t current_ma);
int usbpd_get_status(uint8_t port, usbpd_status_t *status);
int usbpd_send_control(uint8_t port, uint8_t sop, uint8_t type);
int usbpd_send_data_objects(uint8_t port, uint8_t sop, uint8_t type,
                            const uint32_t *objects, uint8_t count);
int usbpd_send_extended(uint8_t port, uint8_t sop, uint8_t type,
                        const uint8_t *data, uint16_t length);
```

`usbpd_init()` 只接受 `port == 0`。应用需要先填好 `usbpd_config_t`，然后周期调用 `usbpd_task(0, now_ms)`。`now_ms` 必须是单调递增的毫秒计数，协议层所有定时器都依赖它。

`usbpd_request()` 用于 Sink 合同重协商。它接受目标电压和电流，策略层会在当前 Source Capabilities 中选择可满足的 PDO 或 APDO。Source 模式下该接口没有合同请求意义。

`usbpd_get_status()` 返回统一状态：

```c
typedef struct usbpd_status_t {
    uint8_t port_type;
    uint8_t power_role;
    uint8_t attached;
    uint8_t reserved;
    union {
        struct pd_sink_status_t sink;
        struct pd_source_status_t source;
    } policy;
} usbpd_status_t;
```

`power_role` 可能是 `USBPD_POWER_ROLE_SINK`、`USBPD_POWER_ROLE_SOURCE` 或 `USBPD_ROLE_INACTIVE`。DRP 未附着时通常为 inactive。

`usbpd_send_control()`、`usbpd_send_data_objects()`、`usbpd_send_extended()` 是原始消息发送接口，供 DPM 或调试路径发送协议支持但策略层未主动发起的消息。调用方需要保证消息类型、对象数量和当前策略状态合法。

常用返回值：

| 返回值 | 含义 |
| --- | --- |
| `USBPD_OK` | 成功 |
| `USBPD_BUSY` | 协议层正在等待 GoodCRC 或发送队列忙 |
| `USBPD_ERR_PARAM` | 参数非法 |
| `USBPD_ERR_UNSUPPORTED` | 端口或能力不支持 |
| `USBPD_ERR_STATE` | 当前状态不允许该操作 |
| `USBPD_ERR_TIMEOUT` | 等待超时 |
| `USBPD_ERR_PROTOCOL` | 协议错误 |
| `USBPD_ERR_OVERFLOW` | 缓冲区溢出或帧超过容量 |

## 5. 配置结构

`usbpd_config_t` 同时包含端口类型、DRP 参数、Sink 配置和 Source 配置：

```c
typedef struct usbpd_config_t {
    uint8_t port_type;
    uint8_t preferred_role;
    uint16_t drp_toggle_ms;
    struct pd_sink_config_t sink;
    struct pd_source_config_t source;
} usbpd_config_t;
```

`port_type` 取 `USBPD_PORT_SINK`、`USBPD_PORT_SOURCE` 或 `USBPD_PORT_DRP`。`preferred_role` 用于 DRP 初始偏好，可填 `USBPD_POWER_ROLE_SINK` 或 `USBPD_POWER_ROLE_SOURCE`。`drp_toggle_ms` 为 DRP Rd/Rp 切换周期，填 0 时使用 `USBPD_DRP_TOGGLE_DEFAULT_MS`，当前默认 75 ms。

## 6. Sink 配置

头文件：`inc/pd_sink.h`

```c
struct pd_sink_config_t {
    union usbpd_pdo_u sink_pdo[USBPD_MAX_DATA_OBJ];
    union usbpd_pdo_u epr_sink_pdo[USBPD_MAX_EPR_DATA_OBJ];
    struct usbpd_sink_cap_ext_db_t sink_cap_ext;
    uint32_t max_voltage_mv;
    uint32_t max_current_ma;
    uint32_t max_power_mw;
    uint8_t sink_pdo_count;
    uint8_t epr_sink_pdo_count;
    uint8_t sink_cap_ext_valid;
    struct pd_sink_features_t features;
    struct usbpd_dpm_callbacks_t dpm;
};
```

`sink_pdo[]` 是本设备发布给对端的 SPR Sink Capabilities，最多 7 个对象。`epr_sink_pdo[]` 是 EPR Sink Capabilities，最多 11 个对象。`max_voltage_mv`、`max_current_ma`、`max_power_mw` 是策略选择上限，策略层不会主动请求超过这些限制的合同。

`features` 控制可选能力：

| 字段 | 含义 |
| --- | --- |
| `pps` | 允许选择 PPS APDO，并按 PPS 周期重发 Request |
| `avs` | 允许选择 SPR AVS 或 EPR AVS |
| `epr` | 允许在满足条件时进入 EPR |
| `usb_communications` | 固定 PDO 中声明 USB 通信能力 |
| `trace_print` | 预留诊断开关，实际打印建议走 observer |

Sink 状态：

| 状态 | 含义 |
| --- | --- |
| `PD_SINK_UNATTACHED` | 未附着，配置 Rd，等待 Source |
| `PD_SINK_ATTACH_WAIT` | 检测到 Rp，等待附着稳定 |
| `PD_SINK_DISCOVERY` | 等待 Source Capabilities |
| `PD_SINK_EPR_CAPS` | 读取或处理 EPR Source Capabilities |
| `PD_SINK_EPR_ENTER` | 发起 Enter EPR 流程 |
| `PD_SINK_REQUESTED` | 已发送 Request，等待 Accept/Wait/Reject |
| `PD_SINK_TRANSITION` | 已 Accept，等待 PS_RDY |
| `PD_SINK_READY` | 显式合同有效，正常运行 |
| `PD_SINK_SOFT_RESET` | Soft Reset 恢复 |
| `PD_SINK_HARD_RESET` | Hard Reset 恢复 |
| `PD_SINK_ERROR_RECOVERY` | 错误恢复，回到未附着路径 |

## 7. Source 配置

头文件：`inc/pd_source.h`

```c
struct pd_source_config_t {
    union usbpd_pdo_u source_pdo[USBPD_MAX_DATA_OBJ];
    union usbpd_pdo_u sink_pdo[USBPD_MAX_DATA_OBJ];
    struct usbpd_source_cap_ext_db_t source_cap_ext;
    struct usbpd_sink_cap_ext_db_t sink_cap_ext;
    union usbpd_source_info_do_u source_info;
    uint8_t source_pdo_count;
    uint8_t sink_pdo_count;
    uint8_t source_cap_ext_valid;
    uint8_t sink_cap_ext_valid;
    uint8_t source_info_valid;
    struct pd_source_features_t features;
    struct usbpd_dpm_callbacks_t dpm;
};
```

Source 模式必须配置 `source_pdo[]`，且第一个 PDO 必须是可安全上电的 5 V 能力。策略层收到有效 Request 后先发送 Accept，然后通过 DPM 板级回调切换电源。只有 `source_ready()` 确认输出达到目标电压后，策略层才发送 PS_RDY。

`sink_pdo[]`、`sink_cap_ext` 和 `source_info` 用于 DRP 或对端查询相关能力。它们不会让当前 Source 自动执行角色切换。

Source 状态：

| 状态 | 含义 |
| --- | --- |
| `PD_SOURCE_UNATTACHED` | 未附着，配置 Rp，等待 Sink |
| `PD_SOURCE_ATTACH_WAIT` | 检测到 Rd，等待附着稳定 |
| `PD_SOURCE_STARTUP` | 初始化 Source 协议状态 |
| `PD_SOURCE_SEND_CAPABILITIES` | 发送 Source Capabilities |
| `PD_SOURCE_WAIT_REQUEST` | 等待 Sink Request |
| `PD_SOURCE_TRANSITION` | 已 Accept，等待板级电源 ready |
| `PD_SOURCE_READY` | 显式合同有效，正常供电 |
| `PD_SOURCE_SOFT_RESET` | Soft Reset 恢复 |
| `PD_SOURCE_HARD_RESET` | Hard Reset 恢复 |
| `PD_SOURCE_ERROR_RECOVERY` | 错误恢复 |

## 8. DPM 回调

DPM 是策略层与应用/板级电源管理之间的接口：

```c
struct usbpd_dpm_callbacks_t {
    usbpd_dpm_message_fn message_received;
    usbpd_dpm_get_extended_fn get_extended;
    usbpd_dpm_event_fn event;
    usbpd_dpm_set_source_fn set_source;
    usbpd_dpm_source_ready_fn source_ready;
    usbpd_dpm_set_switch_fn set_vconn;
    usbpd_dpm_set_switch_fn set_discharge;
    void *context;
};
```

`message_received()` 用于处理策略层不主动消费的控制、数据或扩展消息。返回 `USBPD_OK` 表示应用已经消费，返回 `USBPD_ERR_UNSUPPORTED` 表示策略层应走默认拒绝路径。

`get_extended()` 用于按需生成扩展消息响应。例如 Manufacturer Info、Battery Cap、Country Info 等与产品信息相关的数据不应该写死在协议层。

`event()` 用于通知附着、脱离、合同建立、合同丢失、进入 EPR、退出 EPR、Hard Reset 和协议错误。`USBPD_DPM_CONTRACT` 事件的 `value0/value1` 分别携带电压和电流，其它事件通常传 0。

`set_source()`、`source_ready()`、`set_discharge()` 是 Source 模式的关键安全边界。策略层只决定协议时序，实际电源开关、升降压、限流和放电必须由板级代码完成。

## 9. 协议层职责

头文件：`inc/usbpd_protocol.h`

协议层处理以下内容：

| 内容 | 实现职责 |
| --- | --- |
| Message ID | 发送方向按 SOP 独立维护，匹配 GoodCRC 后递增 |
| GoodCRC | 接收后由 PHY 快速响应，协议层识别发送完成事件 |
| 重传 | 未收到匹配 GoodCRC 时按 `USBPD_N_RETRY_COUNT` 重传 |
| 重复帧过滤 | 同一 SOP、Message ID、类型、对象数的重复帧不重复提交策略层 |
| Soft Reset | 清 Message ID，通知策略层走软复位路径 |
| Hard Reset | 发送或接收硬复位，通知策略层恢复电源安全状态 |
| 扩展消息 | 支持分块发送、分块请求、接收组包和长度检查 |
| 错误上报 | 溢出、畸形帧、分块异常等转成协议事件 |

协议事件包括：

| 事件 | 含义 |
| --- | --- |
| `USBPD_PROTOCOL_RX` | 收到普通控制/数据消息 |
| `USBPD_PROTOCOL_TX_GOODCRC` | 本次发送已被对端 GoodCRC 确认 |
| `USBPD_PROTOCOL_TX_TIMEOUT` | 发送等待 GoodCRC 超时 |
| `USBPD_PROTOCOL_SOFT_RESET` | Soft Reset 事件 |
| `USBPD_PROTOCOL_HARD_RESET` | Hard Reset 事件 |
| `USBPD_PROTOCOL_EXT_RX` | 收到完整扩展消息 |
| `USBPD_PROTOCOL_RX_OVERFLOW` | PHY 接收队列溢出 |
| `USBPD_PROTOCOL_ERROR` | 协议错误 |

策略层不直接操作 Message ID，也不直接判断 GoodCRC。策略层只发起消息、接收事件，并根据合同状态推进。

## 10. 消息定义

`usbpd_def.h` 是唯一协议定义入口，旧的 `usbpd_types.h` 只做兼容转发。代码中保留了 R3.2 V1.1 中所有控制消息、数据消息和扩展消息编码，包括保留编码，方便抓包和日志准确显示。

控制消息包括 GoodCRC、Accept、Reject、Wait、Soft Reset、Data Reset、Not Supported、Get Source Cap Extended、Get Status、Get PPS Status、Get Sink Cap Extended、Get Source Info、Get Revision 等。

数据消息包括 Source Capabilities、Request、BIST、Sink Capabilities、Battery Status、Alert、Get Country Info、Enter USB、EPR Request、EPR Mode、Source Info、Revision、Vendor Defined 等。

扩展消息包括 Source Capabilities Extended、Status、Get Battery Cap、Get Battery Status、Battery Cap、Get Manufacturer Info、Manufacturer Info、Security、Firmware Update、PPS Status、Country Info、Country Codes、Sink Capabilities Extended、Extended Control、EPR Source Capabilities、EPR Sink Capabilities、Vendor Defined 等。

位域联合体采用 `raw + bits` 形式，例如 Header、Extended Header、PDO、RDO、VDM 和 EPR 对象。代码假设目标为小端，非小端 GCC 目标会在编译期报错。主机测试会校验关键对象大小和原始值编解码，避免位域布局被无声破坏。

## 11. PDO 与 RDO 单位

常用对象单位如下：

| 对象 | 字段 | 单位 |
| --- | --- | --- |
| Fixed PDO | voltage/current | 50 mV / 10 mA |
| Variable PDO | min/max voltage/current | 50 mV / 10 mA |
| Battery PDO | min/max voltage/power | 50 mV / 250 mW |
| PPS APDO | min/max voltage/current | 100 mV / 50 mA |
| SPR AVS APDO | min/max voltage/power | 100 mV / 1 W |
| EPR AVS APDO | min/max voltage/power | 100 mV / 1 W |
| Fixed/Battery RDO | operating/max current or power | 10 mA 或 250 mW |
| PPS RDO | output voltage/current | 20 mV / 50 mA |
| AVS RDO | output voltage/current | 25 mV / 50 mA |

策略层对 PDO/RDO 做单位换算，应用层配置时建议使用辅助宏或明确注释，避免把 mV/mA 与协议单位混用。

## 12. Sink 策略行为

Sink 附着后先等待 Source Capabilities。收到 Source Capabilities 后，策略层按配置上限和能力开关选择最合适的 PDO。优先级大致为：满足目标请求的 APDO/AVS/PPS，其次固定或可变能力，最后回落到安全的 5 V 合同。

SPR 合同流程：

```text
Attach -> Source_Capabilities -> Request -> Accept -> PS_RDY -> READY
```

PPS 合同建立后，策略层会按 `USBPD_T_PPS_REQUEST_MS` 周期重发 Request，避免 PPS 超时。对端返回 Wait 或 Reject 时，策略层不会强行维持无效合同，而是按当前状态重新发现或回退。

Soft Reset 会重置协议 Message ID，但 Sink 显式合同按规范保留，之后重新进入可通信状态。Hard Reset 会丢失合同并进入电源安全恢复路径。

## 13. EPR Sink 行为

EPR 只在以下条件满足时进入：

| 条件 | 说明 |
| --- | --- |
| 已有 SPR 显式合同 | 未完成 SPR 合同前不进入 EPR |
| `features.epr != 0` | 应用明确允许 EPR |
| 有 EPR Sink PDO | `epr_sink_pdo_count > 0` |
| 对端能力满足 | Source 发布 EPR Source Capabilities |
| 电压/功率目标需要或允许 EPR | 策略选择时不会无故进入更高风险电压域 |

进入流程包括请求 EPR Source Capabilities、发送 EPR Request/Enter、处理 Enter Acknowledged 与 Enter Succeeded、再请求 EPR 合同。EPR 模式下 Sink 需要处理 KeepAlive，并在异常、Reject、Wait 或降压退出时回到 20 V 及以下的 SPR 安全范围。

当前实现侧重 Sink EPR。Source 侧不发布 EPR Source 能力，也不承担 EPR Source 电源安全责任。

## 14. Source 策略行为

Source 附着后发布 Source Capabilities，等待 Sink Request。收到 Request 后，策略层检查对象位置、能力匹配、GiveBack/CapabilityMismatch、PPS/AVS 单位、最大电流或功率限制。合法时发送 Accept，然后调用 `set_source()` 切换板级电源，等待 `source_ready()` 返回真后发送 PS_RDY。

Source 模式下 PPS 合同需要 Sink 周期性刷新 Request。若超时，策略层会丢失 PPS 合同并回到安全状态。收到 Hard Reset 时，Source 会关闭输出、启用放电回调，并重新进入能力发布流程。收到对端 Hard Reset 不会再反向发送第二个 Hard Reset。

不支持的角色切换、废弃行为或 Source/DRP 专属能力会走 Reject、Wait 或 Not Supported 路径。

## 15. DRP 行为

DRP 模式由 `src/usbpd.c` 统一协调。未附着时，驱动按 `drp_toggle_ms` 在 Rd 和 Rp 之间切换。检测到 Source 侧 Rp 时进入 Sink 策略，检测到 Sink 侧 Rd 时进入 Source 策略。

DRP 附着后不会继续切换角色。脱离或错误恢复后，端口回到 toggle 状态。`preferred_role` 只影响初始切换方向，不代表强制角色。

## 16. Swap 策略

当前驱动对 `PR_Swap`、`DR_Swap`、`VCONN_Swap` 都返回 `Reject`，不会进入角色切换流程。

| Swap | 当前响应 | 原因 |
| --- | --- | --- |
| `PR_Swap` | `Reject` | 需要 Source/Sink 电源路径无缝切换、VBUS 放电/上电互锁、合同重建和故障保护，当前 CH32X035 后端没有实现完整板级闭环 |
| `DR_Swap` | `Reject` | 需要 USB 数据角色和协议栈同步切换；当前驱动只维护 PD Header 的 data role，不接管 USB Device/Host 栈 |
| `VCONN_Swap` | `Reject` | 需要 VCONN 供电开关、过流保护、线缆供电时序和异常恢复；当前仅保留 `set_vconn` 回调位置，不主动驱动 VCONN |

这里选择 `Reject` 而不是静默忽略，是为了让对端立即得到明确的规范负响应。测试中已经覆盖 Sink 和 Source 收到这三类 Swap 后均发送 `Reject`。未来若要支持，应先扩展 DPM 回调和板级状态机，再把策略层从拒绝路径改成 Accept/切换/PS_RDY 或对应完成消息流程。

## 17. CH32X035 PHY

头文件：`platform/ch32x035/usbpd_phy_ch32x035.h`

```c
int usbpd_phy_init(void);
void usbpd_phy_task(uint32_t now_ms);
void usbpd_phy_set_cc(uint8_t cc);
void usbpd_phy_set_pull(uint8_t pull);
void usbpd_phy_set_rp(uint32_t current_ma);
void usbpd_phy_set_roles(uint8_t power_role, uint8_t data_role);
void usbpd_phy_get_cc(enum usbpd_cc_e *cc1, enum usbpd_cc_e *cc2);
int usbpd_phy_get_vbus(uint32_t *mv);
int usbpd_phy_send(uint8_t sop, const uint8_t *raw, uint8_t len, uint32_t now_ms);
int usbpd_phy_receive(struct usbpd_frame_t *frame);
uint8_t usbpd_phy_tx_idle(void);
uint8_t usbpd_phy_take_rx_overflow(void);
void usbpd_phy_reset_rx(void);
```

PHY 层独占：

| 资源 | 说明 |
| --- | --- |
| USBPD 寄存器 | BMC 收发、SOP/Hard Reset、CRC、状态位 |
| DMA | 收发缓冲搬运 |
| CC 比较器 | Rd/Rp/Ra/Open 判断与极性选择 |
| VBUS 测量 | 返回 mV 给策略层 |
| IRQ | 接收队列、发送完成、错误状态 |
| GoodCRC 快速响应 | 中断内完成低延迟 GoodCRC |

上层不包含芯片头文件。CH32X035 工程需要提供 WCH SDK 中对应的寄存器定义、USBPD 外设宏、时钟和中断启动代码，以及板级 VBUS 测量实现。没有这些 SDK 头文件时，主机测试仍可通过 fake PHY 编译。

## 18. 消息缓冲与诊断

通用缓冲区位于 `common/message_buffer/`，不理解 USB PD，也不依赖硬件。每条记录包含时间戳、方向、类别、标志、长度和原始载荷。默认容量为 32 条，满后覆盖最旧记录，并增加 dropped 计数。

PD 诊断接口位于 `inc/usbpd_message.h`：

```c
void usbpd_observer_init(void);
void usbpd_observer_record(uint8_t direction, uint8_t sop, const uint8_t *raw,
                           uint8_t length, uint32_t timestamp_ms, uint8_t flags);
uint8_t usbpd_observer_pop(struct message_buffer_record_t *record);
void usbpd_observer_clear(void);
uint16_t usbpd_observer_count(void);
uint32_t usbpd_observer_dropped(void);
void usbpd_observer_service(uint8_t max_records,
                            usbpd_observer_write_fn write,
                            void *arg);
```

ISR 和任务上下文都可以写入 observer，但读取、解码和打印只能在任务上下文做。中断内不要打印。推荐应用在主循环中调用：

```c
static void pd_log_write(const char *text, void *arg)
{
    (void)arg;
    puts(text);
}

void app_task(uint32_t now_ms)
{
    usbpd_task(0, now_ms);
    usbpd_observer_service(4, pd_log_write, NULL);
}
```

日志会输出方向、SOP、消息名、Message ID、对象数量、扩展分块信息、PDO/RDO 摘要、错误状态和原始十六进制数据。这样可以在没有 PD 分析仪时快速确认协商路径；有分析仪时则可对照 Message ID 和原始帧。

## 19. 示例：Sink 端口

下面示例只展示结构填充方式。PDO 位域字段请以 `usbpd_def.h` 中当前联合体为准。

```c
static usbpd_config_t g_pd;

void board_pd_init(void)
{
    memset(&g_pd, 0, sizeof(g_pd));

    g_pd.port_type = USBPD_PORT_SINK;

    g_pd.sink.max_voltage_mv = 20000;
    g_pd.sink.max_current_ma = 3000;
    g_pd.sink.max_power_mw = 60000;
    g_pd.sink.features.pps = 1;
    g_pd.sink.features.avs = 1;
    g_pd.sink.features.epr = 0;
    g_pd.sink.features.usb_communications = 1;

    g_pd.sink.sink_pdo_count = 1;
    g_pd.sink.sink_pdo[0].raw = 0;
    g_pd.sink.sink_pdo[0].sink_fixed.type = USBPD_PDO_FIXED;
    g_pd.sink.sink_pdo[0].sink_fixed.voltage_50mv = 100;  /* 5 V */
    g_pd.sink.sink_pdo[0].sink_fixed.current_10ma = 300;

    usbpd_init(0, &g_pd);
}
```

主循环：

```c
void main_loop(void)
{
    uint32_t now_ms = board_millis();

    usbpd_task(0, now_ms);
    usbpd_observer_service(8, board_log_write, NULL);
}
```

请求新电压：

```c
void request_9v(void)
{
    (void)usbpd_request(0, 9000, 3000);
}
```

## 20. 示例：Source 端口

Source 必须提供电源控制回调：

```c
static int board_set_source(uint8_t port, uint8_t enable,
                            uint32_t mv, uint32_t ma, void *ctx)
{
    (void)port;
    (void)ctx;

    if (!enable) {
        board_power_disable();
        return USBPD_OK;
    }

    board_power_set_limit(ma);
    board_power_set_voltage(mv);
    board_power_enable();
    return USBPD_OK;
}

static uint8_t board_source_ready(uint8_t port, uint32_t mv, void *ctx)
{
    (void)port;
    (void)ctx;
    return board_vbus_is_near(mv);
}
```

配置 Source：

```c
static usbpd_config_t g_pd;

void board_pd_source_init(void)
{
    memset(&g_pd, 0, sizeof(g_pd));

    g_pd.port_type = USBPD_PORT_SOURCE;
    g_pd.source.features.pps = 1;
    g_pd.source.features.usb_communications = 1;

    g_pd.source.source_pdo_count = 1;
    g_pd.source.source_pdo[0].raw = 0;
    g_pd.source.source_pdo[0].fixed.type = USBPD_PDO_FIXED;
    g_pd.source.source_pdo[0].fixed.voltage_50mv = 100; /* 5 V */
    g_pd.source.source_pdo[0].fixed.current_10ma = 300;

    g_pd.source.dpm.set_source = board_set_source;
    g_pd.source.dpm.source_ready = board_source_ready;
    g_pd.source.dpm.set_discharge = board_set_discharge;

    usbpd_init(0, &g_pd);
}
```

Source 第一条 PDO 应始终是 5 V 固定供电。高压 PDO 只有在 Request、Accept、电源切换和 PS_RDY 时序完成后才可以输出。

## 21. 示例：DRP 端口

```c
static usbpd_config_t g_pd;

void board_pd_drp_init(void)
{
    memset(&g_pd, 0, sizeof(g_pd));

    g_pd.port_type = USBPD_PORT_DRP;
    g_pd.preferred_role = USBPD_POWER_ROLE_SINK;
    g_pd.drp_toggle_ms = 75;

    /* 同时填充 g_pd.sink 和 g_pd.source。 */

    usbpd_init(0, &g_pd);
}
```

DRP 模式下，Sink 和 Source 两侧都应配置完整。作为 Source 附着时，`source.dpm.set_source` 与 `source.dpm.source_ready` 仍然是必须的。

## 22. 构建与主机测试

主机测试使用 fake PHY 验证协议和策略，不需要 CH32X035 SDK：

```powershell
cmake -S USB_PD -B USB_PD/build-host -DUSBPD_HOST_TESTS=ON
cmake --build USB_PD/build-host --config Debug
ctest --test-dir USB_PD/build-host -C Debug --output-on-failure
```

当前测试覆盖：

| 测试方向 | 内容 |
| --- | --- |
| 位域和对象 | Header、Extended Header、PDO/RDO 原始值和大小断言 |
| 帧编解码 | Header、Data Objects、CRC 长度和畸形帧拒绝 |
| 通用缓冲区 | 环形覆盖、dropped 计数、读取和清空 |
| 诊断格式化 | 消息名、PDO/RDO 摘要、raw hex |
| 协议层 | GoodCRC、Message ID、重传、重复帧、分块扩展消息、复位 |
| Sink 策略 | SPR/PPS/EPR、Source Capabilities、超时和复位 |
| Source 策略 | Request 评估、PS_RDY 时序、PPS 超时、扩展响应 |
| DRP | Rd/Rp 切换、角色附着、脱离恢复 |

## 23. CH32X035 实机验证

实机验证建议至少覆盖以下项目：

| 项目 | 目标 |
| --- | --- |
| CC1/CC2 插入方向 | 两个方向都能附着、脱离、重新附着 |
| 固定 PDO | 5 V、9 V、15 V、20 V 等合同与 VBUS 实测一致 |
| PPS | 电压步进、限流、周期 Request 和超时恢复 |
| SPR AVS | 请求单位、Accept/PS_RDY 和异常恢复 |
| EPR Sink | SPR 合同后进入 EPR，非 EPR 线缆拒绝或失败恢复 |
| Soft Reset | Message ID 复位，合同按规范保留 |
| Hard Reset | VBUS 安全关闭或回落，重新发现 |
| GoodCRC 丢包 | 原 Message ID 重发两次，确认后再递增 |
| 扩展消息 | Source Cap Ext、Sink Cap Ext、Status、PPS Status、Revision、Source Info |
| Observer | 日志完整、无中断内打印、dropped 计数可解释 |
| Source 模式 | Accept 后等待板级电源 ready，再发送 PS_RDY |
| DRP 模式 | 75 ms 切换、Sink/Source 双向附着、脱离后恢复切换 |

详细清单见 `doc/CH32X035_VALIDATION.md`。

## 24. 迁移说明

旧接口迁移关系：

| 旧接口或旧模块 | 新位置 |
| --- | --- |
| `pd_sink_*` 应用直接调用 | 应用改用 `usbpd_*`，Sink 策略内部仍保留 `pd_sink_*` |
| `usb_pd_*` 示例接口 | 删除，改用 `usbpd_init/task/request/get_status` |
| `usbpd_platform_ops_t` | 删除 |
| `usbpd_platform_set_ops` | 删除 |
| `pd_trace` | 替换为 `common/message_buffer` + `usbpd_observer_*` |
| `usbpd_types.h` | 兼容转发到 `usbpd_def.h` |

应用层应该只包含 `usbpd.h` 和必要的 `usbpd_def.h`。策略层和协议层内部头文件不建议被应用直接依赖。

## 25. 后续维护规则

新增协议消息时，先更新 `usbpd_def.h` 的枚举、位域或数据块，再更新 `usbpd_message.c` 的可读名称和摘要，最后补主机测试。

新增策略行为时，应放在 `pd_sink.c` 或 `pd_source.c`，不要写进 PHY。PHY 只负责收发帧、CC、VBUS、GoodCRC 快速响应和硬件状态。

新增平台时，提供同名 `usbpd_phy_*` 函数和同一套 fake/契约测试。不要恢复平台函数指针操作表。

涉及 Source 供电、电压升降、放电、过流、过温和 VBUS 掉电的逻辑，必须通过板级 DPM 回调接入真实硬件保护。协议 Accept 不是电源已经安全到位的证明，PS_RDY 才是对外承诺。

## 26. 参考依据

本驱动按 USB Power Delivery R3.2 V1.1 的消息编码、对象单位、GoodCRC/Message ID、扩展消息分块和 Sink/Source 策略时序组织。实现中使用本仓库内的规范 PDF 作为协议依据，并用主机测试固定关键二进制布局。
