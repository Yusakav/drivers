# USB PD Sink 驱动逻辑

## 1. 目标

此驱动面向 CH32X035 的单端口 USB PD Sink（受电端）。驱动不会声明或切换为 Source（供电端）；应用层只需给出目标电压、电流，驱动负责与已连接的 Source 自动协商安全电源合同。

公开接口位于 `inc/pd_sink.h`：

```c
int  pd_sink_init(const struct pd_sink_config_t *config);
void pd_sink_task(uint32_t now_ms);
int  pd_sink_request(uint32_t voltage_mv, uint32_t current_ma);
int  pd_sink_get_status(struct pd_sink_status_t *status);
void pd_sink_trace_service(uint8_t max_records);
void pd_sink_trace_clear(void);
```

`now_ms` 是应用层提供的单调递增毫秒计数。驱动不依赖特定的 `timer.h`，便于移植与测试。

## 2. 分层

```text
应用层
    pd_sink_init / pd_sink_request / pd_sink_task
                |
Sink 策略层: pd_sink.c
    Type-C 附着、PDO 选择、PPS/EPR 策略、错误恢复
                |
PD 协议层: usbpd_protocol.c
    Header、Message ID、GoodCRC、复位、扩展消息分块
                |
设备驱动层: usbpd_device.c
    CH32X035 USBPD 寄存器、CC 比较器、DMA/BMC、USBPD_IRQHandler
```

`USBPD_IRQHandler` 只负责复制接收帧、保存追踪记录和发送 GoodCRC；中断内不打印，也不运行 Sink 状态机。

## 3. 初始化与配置

`pd_sink_init(NULL)` 使用保守的 5V/1A 默认能力。

真实产品应传入 `pd_sink_config_t`，描述真实的 Sink PDO、EPR Sink PDO、最大电压/电流/功率与能力位：

```c
struct pd_sink_config_t config = {
    .max_voltage_mv = 48000,
    .max_current_ma = 5000,
    .max_power_mw = 240000,
    .sink_pdo_count = 0,      /* 填入实际 SPR Sink PDO。 */
    .epr_sink_pdo_count = 0,  /* 填入实际 EPR Sink PDO。 */
    .features = {
        .pps = 1,
        .epr = 1,
        .require_5a_cable = 1,
        .trace_print = 0,
    },
};
```

配置必须与连接器、线缆假设、保护电路和后级电源路径一致。软件中声明 48V/5A 不会使硬件自动具备 48V/5A 安全能力。

## 4. 主循环

应尽可能频繁地调用 `pd_sink_task()`，推荐每 1ms 调用一次。该任务处理 PHY 超时、协议定时器、PD 策略和 Type-C 附着状态。

```c
uint32_t now_ms = app_millis();

pd_sink_task(now_ms);
pd_sink_trace_service(1); /* 可选：本次最多打印一条已保存报文。 */
```

上电或 READY 后可以设置新目标：

```c
pd_sink_request(9000U, 2000U);
```

目标值会跨断开和复位保留。READY 状态修改目标时，驱动利用缓存的 Source Capabilities 自动重新请求。

## 5. Sink 状态流

```text
UNATTACHED
    | CC1 或 CC2 检测到 Rp
    v
ATTACH_WAIT
    | tCCDebounce 到期，选择有效 CC
    v
DISCOVERY <-----------------------------+
    | Source_Capabilities               |
    v                                   |
评估目标 PDO                           |
    |                                   |
    +-- 需要 5A --> CABLE_DISCOVERY（SOP' Discover Identity）
    +-- 需要 EPR -> EPR_CAPS -> EPR_ENTER
    |                                   |
    v                                   |
REQUESTED -- Accept --> TRANSITION -- PS_RDY --> READY
    |                                       |
    +-- Reject/Wait/超时 --> 5V 回退 --------+

Hard Reset / 协议错误 --> ERROR_RECOVERY --> UNATTACHED
```

目标选择优先级：

1. 电压精确匹配且电流足够的 Fixed PDO。
2. 已启用 PPS 时，落在 PPS APDO 范围内的目标。
3. 已启用 EPR 且已验证线缆时，落在 EPR AVS 范围内的目标。
4. Source 的 5V Fixed PDO 作为安全回退。

回退电流同时受请求电流、Sink 配置上限和 Source PDO 上限约束。

## 6. 线缆与 EPR

当目标高于 20V 或高于 3A，且 `require_5a_cable` 已启用时，驱动会通过 SOP' Structured VDM 发送 Discover Identity。只有确认线缆 e-marker 支持 5A 后，才允许高功率请求。

EPR 请求先获取 EPR Source Capabilities，再进入 EPR Mode，随后请求选中的 AVS PDO。EPR 合同生效后周期发送 EPR KeepAlive；从 EPR 返回 SPR 目标时，先退出 EPR 再请求新的 SPR 合同。

线缆验证失败、Source 不支持 EPR、Reject、Wait 重试耗尽或相关超时时，都会回退到 5V Fixed PDO 请求。

## 7. 报文处理

设备层使用 CH32X035 USBPD DMA/BMC 外设：

- 接收 DMA 帧由中断复制到四项 RX 队列。
- 正常 SOP、SOP'、SOP'' 报文会先由中断回复 GoodCRC，再交给任务处理。
- 协议层按 SOP 保存 Message ID，并在回复 GoodCRC 后丢弃重复的数据/控制报文。
- 发送后等待匹配的 GoodCRC，SenderResponse 超时为 30ms。
- Soft Reset 清除协议 Message ID 并重新进入能力发现。
- Hard Reset 清除当前合同并进入错误恢复。
- 扩展消息使用一个共享的 260B 缓冲区，分块收发必须逐块请求；追踪模块保存每个物理分块帧。

## 8. 报文存储与打印

`pd_trace.c` 保存 16 条固定长度记录。每条记录包含时间戳、收发方向、SOP 类型、GoodCRC/错误/复位标志、原始长度和原始字节。队列满时覆盖最旧记录。

RX、TX、GoodCRC 和复位令牌都会保存。打印只能通过 `pd_sink_trace_service()`，或启用 `features.trace_print` 后由 `pd_sink_task()` 每次打印一条。

不要在 `USBPD_IRQHandler` 中加入 `printf`；串口阻塞会破坏 PD 时序。

## 9. 状态读取

调用 `pd_sink_get_status()` 可读取目标请求、实际协商合同、VBUS、电源 PDO、Source PDO 数量、当前状态、回退标志、5A 线缆标志和 EPR 标志。

只有收到 `PS_RDY` 后 `contract_valid` 才会置位。应用层必须使用实际协商结果，而不能只使用请求值，因为回退后合同可能与目标不同。
