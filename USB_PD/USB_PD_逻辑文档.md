# USB PD 协议栈详细逻辑文档

> 本文档覆盖 `e:\Yusaka\varch\Embed\drivers\USB_PD\` 目录下全部源文件的实现逻辑，
> 与代码一一对应，不涉及外部依赖（CH32X035 USBPD 寄存器、ADC、DMA 等）的硬件手册细节。

---

## 1. 整体架构

### 1.1 分层图（自下而上）

```
┌───────────────────────────────────────────────────────────┐
│                    应用层 / 板级策略                        │
│  DPM 回调集合：message_received / event / set_source ...   │
├───────────────────────────────────────────────────────────┤
│  pd_sink.c  │  pd_source.c   策略层（状态机 + PDO 选择）     │
├───────────────────────────────────────────────────────────┤
│               usbpd_protocol.c     协议层                     │
│  MessageID 管理 │ GoodCRC 重发 │ 扩展消息分块 │ SOFT/HARD Reset │
├───────────────────────────────────────────────────────────┤
│    usbpd_frame.c  │  usbpd_message.c     帧层 + 观察器       │
│  长度校验/解码      环形缓冲 + 格式化日志                     │
├───────────────────────────────────────────────────────────┤
│  usbpd_phy_ch32x035.c        PHY 层（CH32X035 硬件绑定）      │
│  CC 检测 │ BMC 收发 │ DMA 缓冲 │ IRQ 自动 GoodCRC │ VBUS ADC │
└───────────────────────────────────────────────────────────┘
```

### 1.2 关键数据通路

| 通路 | 方向 | 参与层 | 说明 |
|------|------|--------|------|
| RX 帧到达 | PHY → 协议 | `USBPD_IRQHandler` → `device_queue_rx` → `usbpd_phy_receive` → `usbpd_frame_decode_wire` → `usbpd_protocol_task` → `protocol_handle_rx` | IRQ 中入环形缓冲，主循环轮询消费 |
| GoodCRC 自动应答 | PHY 内部 | `USBPD_IRQHandler` → `device_send_goodcrc` | 非 GoodCRC 帧在 IRQ 中立即构造并发送 2 字节头，不等协议层 |
| TX 帧发送 | 协议 → PHY | `usbpd_protocol_send_frame` → `usbpd_phy_send` → `device_start_tx` | 协议层缓存原始帧，PHY DMA 异步发送 |
| GoodCRC 重发 | 协议内部 | `usbpd_protocol_task` 超时重发 `g_protocol.tx_raw` | 超过 `USBPD_N_RETRY_COUNT` 上报 `USBPD_PROTOCOL_TX_TIMEOUT` |
| DPM 事件 | 策略层 → 应用 | `sink_dpm_event` / `source_event` → `usbpd_dpm_callbacks_t.event` | CONTRACT/CONTRACT_LOST/ATTACHED/DETACHED/HARD_RESET/PROTOCOL_ERROR/EPR_ENTERED/EPR_EXITED |
| DPM 消息 | 协议 → 应用 | `sink_dpm_message` / `source_dpm_message` → `usbpd_dpm_callbacks_t.message_received` | 未消费则协议层按默认策略回复 |
| 板级电源控制 | Source 策略 → 板级 | `usbpd_dpm_callbacks_t.set_source` / `source_ready` / `set_discharge` | Transition 状态中控制 VBUS 电压切换 |
| 观察器 | 全层 → 调试 | `usbpd_observer_record` → 环形缓冲 → `usbpd_observer_service` | PHY 层在收发成功后登记，协议层/策略层在错误时登记 |

### 1.3 时序参数速查

| 宏 / 常量 | 值 | 所属层 | 含义 |
|-----------|-----|--------|------|
| `USBPD_GOODCRC_TIMEOUT_MS` | 2 | 协议层 | GoodCRC 等待上限（毫秒） |
| `USBPD_N_RETRY_COUNT` | 3 | 协议层 | GoodCRC 超时重发上限 |
| `USBPD_T_SENDER_RESPONSE_MAX_MS` | 500 | 协议层 | 对端响应等待上限（毫秒） |
| `USBPD_RX_DEPTH` | 4 | PHY 层 | 接收环形缓冲深度（帧数量） |
| `USBPD_TX_TIMEOUT_MS` | 5 | PHY 层 | BMC 发送超时（毫秒） |
| `PD_SINK_CC_DEBOUNCE_MS` | 100 | Sink | CC 去抖动（毫秒） |
| `PD_SOURCE_CAP_PERIOD_MS` | 150 | Source | Send_Capabilities 重发周期（毫秒） |
| `PD_SOURCE_MAX_CAP_ATTEMPTS` | 6 | Source | Send_Capabilities 重发上限 |
| `PD_SOURCE_PPS_TIMEOUT_MS` | 13500 | Source | PPS 动态请求超时（毫秒） |
| `PD_SINK_MAX_WAIT_RETRIES` | 2 | Sink | Wait 消息重试上限 |

---

## 2. PHY 层（CH32X035）

### 2.1 接收流程（USBPD_IRQHandler 中断驱动）

```
BMC 解码完成 → IF_RX_ACT 中断
    │
    ├─ 读取 BMC_BYTE_CNT 得到 length
    ├─ device_sop_from_status(status, length) 解码 SOP
    │     SOP1_HRST: length≥6 → SOP', <6 → HardReset
    │     SOP2_CRST: length≥6 → SOP'', <6 → CableReset
    │
    ├─ 若 SOP ≤ DPRIME 且 length ≥ 6 且非 GoodCRC:
    │     Delay_Us(30) → device_send_goodcrc
    │         解析 rx.header.revision / message_id
    │         填充 tx.header.type=GOODCRC, role 位
    │         device_start_tx: LVE → DMA → TX_SEL → BMC_START
    │         g_dev.tx_busy = 1, tx_deadline = now + 5ms
    │
    └─ device_queue_rx(sop, length) → 主循环消费
```

**关键细节**：
- 自动 GoodCRC 应答在 IRQ 中立即构造发送，不经过协议层；协议层仅负责检查 GoodCRC 的 message_id / sop 匹配。
- HardReset / CableReset 不发送 GoodCRC（SOP > DPRIME 时 `device_send_goodcrc` 直接 return）。
- RX DMA 缓冲双缓冲设计：IRQ 写 `g_dev.rx_dma`，拷贝到环形缓冲后 DMA 继续接收下一帧。

### 2.2 发送流程

```
usbpd_protocol_send_frame → usbpd_phy_send(sop, raw, len, now_ms)
    │
    ├─ 参数校验：sop ≤ CABLE_RESET, len ≤ MAX_FRAME_LEN, tx_busy==0
    ├─ memcpy(raw → g_dev.tx_dma)
    ├─ g_dev.tx_busy = 1, tx_deadline = now + 5ms
    ├─ usbpd_observer_record(MESSAGE_BUFFER_TX, ...)
    └─ device_start_tx:
         置 CC_LVE → 配置 TX_SEL / BMC_TX_SZ / DMA → PD_TX_EN + BMC_START
```

**TX 超时强停**（`usbpd_phy_task`）：
- 主循环检测 `(int32_t)(now - tx_deadline) >= 0`，强制关 `PD_TX_EN | BMC_START`，置 `IF_TX_END` 标志后调用 `device_finish_tx`。
- `device_finish_tx`：清 `tx_busy` → 清 CC_LVE → `device_enable_rx`（恢复接收）。

### 2.3 CC 检测流程

```
usbpd_phy_get_cc(&cc1, &cc2)
    │
    └─ device_read_cc(&PORT_CC)
         │
         ├─ 逐档切换比较器阈值（22→45→55→66→95→123 mV）
         │     每档等待 2μs，PA_CC_AI 为 0 时跳出
         │     voltage = 该档阈值
         │
         ├─ 恢复默认 66mV 阈值
         │
         └─ 判断 CC_PD 位：
              CC_PD=1 (Source 上拉):
                voltage≥123 → RP_3A (25.6kΩ)
                voltage≥66  → RP_1A5 (12kΩ)
                voltage≥22  → RP_DEF (5.1kΩ)
                否则        → Open
              CC_PD=0 (Sink 下拉):
                voltage≥22  → RD (5.1kΩ)
                否则        → RA (未连接)
```

**Rp 电流档位**（`usbpd_phy_set_rp(current_ma)`）：
- ≥3000mA → CC_PU_330（1.0A）—— 注意：硬件寄存器值命名与实际 Rp 电流档位需对照 CH32X035 数据手册。
- ≥1500mA → CC_PU_180
- 默认 → CC_PU_80
- 若 CC_PU_80 / CC_PU_180 宏未定义，固定使用 CC_PU_330（兼容模式）。

### 2.4 双环形队列

| 队列 | 深度 | 读写指针 | 溢出策略 | 消费方 |
|------|------|----------|----------|--------|
| `g_dev.rx` | 4 | `rx_read` / `rx_write` / `rx_count` | 丢弃最旧帧，置 `rx_overflow=1` | `usbpd_phy_receive`（协议层） |
| `g_dev.goodcrc_trace` | 4 | `goodcrc_read` / `goodcrc_write` / `goodcrc_count` | 丢弃最旧帧 | `usbpd_phy_task` 刷写到观察器 |

**中断保护**：环形队列读写均需 `__disable_irq()` / `__enable_irq()` 包裹；GoodCRC trace 的写入在 IRQ 中，读取在主循环中，也需保护。

### 2.5 VBUS 采集

```
usbpd_phy_get_vbus(&mv)
    │
    ├─ adc_get_vbus_mv() 复用 ADC 通道采集 VBUS 分压
    ├─ 负值钳位为 0
    └─ ≥800mV → USBPD_OK, <800mV → USBPD_BUSY（表示 VBUS 未就绪）
```

---

## 3. 帧层（usbpd_frame）

### 3.1 长度校验（usbpd_frame_wire_length_valid）

```
输入: wire[0..N], wire_length
    │
    ├─ 基础校验：wire≠NULL, wire_length ≥ 6 (2头+4CRC), wire_length ≤ USBPD_TRACE_FRAME_LEN
    ├─ 计算期望长度: expected = 2 + header.data_objects × 4 + USBPD_CRC_LEN(4)
    ├─ wire_length ≠ expected → 非法
    └─ 扩展消息特例: header.extended=1 且 data_objects=0 → 非法（无空间放 2 字节扩展头）
```

### 3.2 解码（usbpd_frame_decode_wire）

```
输入: wire, wire_length, sop, frame
    │
    ├─ 参数校验 + 长度二次校验
    ├─ frame.header.bytes[0/1] = wire[0/1]
    ├─ frame.payload = &wire[2]，长度 = data_objects × 4
    ├─ frame.payload_len = data_objects（注意：以 DO 个数表示，不是字节数）
    ├─ frame.sop = sop
    └─ frame.raw_len = wire_length
```

---

## 4. 协议层（usbpd_protocol）

### 4.1 MessageID 管理

```
uint8_t tx_id[3]   /* SOP/SOP'/SOP'' 各自独立的发送序号（0..7 循环） */
uint8_t rx_id[3]   /* 最近接收的序号 */
uint8_t rx_id_valid : 3  /* 每位对应一个通道 rx_id 是否有效 */
```

**发送侧**：`protocol_fill_header` 取 `tx_id[index]` 填入 header.bits.message_id，发送后等待 GoodCRC 确认；收到匹配 GoodCRC 后 `tx_id[index] = (tx_id + 1) & 0x07`。

**接收侧**：收到帧后检查 `rx_id_valid & (1<<index)` 且 `rx_id[index] == 收到的 message_id`，若匹配则丢弃（重复帧去重）；否则更新 `rx_id[index]` 与 `rx_id_valid`。

### 4.2 GoodCRC 等待与重发

```
protocol_send_frame:
    g_protocol.awaiting_crc = 1
    g_protocol.awaiting_sop / awaiting_id = 当前帧 SOP / MessageID
    g_protocol.deadline = now + 2ms
    g_protocol.tx_retry_count = 0
    memcpy(g_protocol.tx_raw, raw, len)  /* 缓存原始帧用于重发 */

protocol_handle_rx (收到 GoodCRC):
    sop 匹配 && id 匹配:
        g_protocol.awaiting_crc = 0
        tx_id[index]++
        若 ext_tx_active 且未发完 → ext_tx_wait_request = 1
        protocol_emit_tx_goodcrc()  /* 上报 USBPD_PROTOCOL_TX_GOODCRC */

usbpd_protocol_task (超时):
    deadline 到期:
        tx_retry_count < 3 → 重发 tx_raw, retry_count++, deadline 重置
        tx_retry_count ≥ 3 → awaiting_crc=0, 上报 USBPD_PROTOCOL_TX_TIMEOUT
```

### 4.3 SOFT Reset 处理

```
收到 SOFT_RESET 控制消息:
    tx_id[index] = 0           /* 发送序号归零 */
    rx_id_valid &= ~(1<<index) /* 清除接收有效性 */
    protocol_emit(USBPD_PROTOCOL_SOFT_RESET)  /* 上报策略层 */

策略层收到 SOFT_RESET 事件后自行决定是否回复 ACCEPT
```

### 4.4 HARD Reset 处理

```
收到 HARD_RESET (PHY 层 SOP=HARD_RESET):
    protocol_emit(USBPD_PROTOCOL_HARD_RESET)  /* 上报策略层 */
    /* 协议层不主动复位状态，由策略层调用 usbpd_protocol_reset() */

发送 HARD_RESET:
    usbpd_protocol_send_hard_reset → usbpd_phy_send(HARD_RESET)
    /* 直接经 PHY 层发送，不等 GoodCRC */
```

### 4.5 扩展消息分块收发

**发送侧（ext_tx_active）**：
```
usbpd_protocol_send_extended_sop:
    memcpy g_protocol.ext_tx_data, data, length
    g_protocol.ext_tx_active = 1, chunk = 0, offset = 0
    protocol_send_chunk()  /* 首块立即发送 */

protocol_send_chunk:
    data_bytes = min(USBPD_EXT_CHUNK_DATA_MAX, 剩余)
    构造扩展头: data_size / chunk_number / chunked
    payload[0..1] = 扩展头, payload[2..N] = 数据
    object_count = (data_bytes + 2 + 3) / 4  /* 向上对齐到 4 字节 */
    protocol_send_frame() → 等待 GoodCRC

收到 GoodCRC 后 ext_tx_wait_request = 1:
    等待对端 Chunk Request 控制消息...
收到 Chunk Request (ext_tx_request_pending = 1):
    protocol_send_chunk() → 下一块
    ext_tx_offset += data_bytes, chunk++
全部发完: ext_tx_active = 0
```

**接收侧（ext_rx_active）**：
```
收到扩展消息帧 (header.extended=1):
    protocol_handle_extended()
    
    若是 Chunk Request (ext.bits.request_chunk=1):
        校验 chunk_number 匹配本端已发 ext_tx_chunk
        ext_tx_request_pending = 1
    
    否则是数据块:
        校验 ext.bits.data_size / chunked 合法性
        chunk_bytes = frame->payload_len × 4 - 2
        offset = chunk_number × USBPD_EXT_CHUNK_DATA_MAX
        
        首块(chunk_number==0):
            g_protocol.ext_rx_length = data_size
            g_protocol.ext_rx_offset = 0
            g_protocol.ext_rx_active = chunked  /* 非分块则不置位 */
        
        memcpy(&ext_rx_data[offset], &payload[2], chunk_bytes)
        更新 ext_rx_offset
        
        若 offset+chunk_bytes < ext_rx_length:
            g_protocol.ext_rx_chunk++
            g_protocol.ext_rx_request_pending = 1  /* 等一下要发 Chunk Request */
        否则:
            g_protocol.ext_rx_active = 0
            protocol_emit(USBPD_PROTOCOL_EXT_RX)  /* 完整重组 */

usbpd_protocol_task:
    ext_rx_request_pending && !awaiting_crc:
        protocol_request_next_chunk() → 发送 Chunk Request 控制消息
        对端收到后发下一数据块
```

**分块超时**：
```
usbpd_protocol_task:
    (ext_tx_wait_request || ext_rx_wait_chunk) && deadline 到期:
        ext_tx_active = 0, ext_rx_active = 0
        ext_tx_wait_request = 0, ext_rx_wait_chunk = 0
        上报 USBPD_PROTOCOL_TX_TIMEOUT
```

---

## 5. 观察器（usbpd_message）

### 5.1 数据结构

```
struct message_buffer_t  /* 环形缓冲（由外部 common/message_buffer 模块提供） */
    每个记录: timestamp_ms | direction | channel(SOP) | flags | length | payload[]
```

### 5.2 记录登记点

| 登记位置 | direction | flags | 说明 |
|----------|-----------|-------|------|
| `usbpd_phy_send` | TX | `sop≥HARD_RESET` → `OBSERVER_RESET`, 否则 0 | PHY 发起发送 |
| `usbpd_phy_receive` | RX | `sop≥HARD_RESET` → `OBSERVER_RESET` | PHY 接收帧入队 |
| `usbpd_phy_task` 刷写 GoodCRC trace | TX | `OBSERVER_GOODCRC` | 自动 GoodCRC 应答 |
| `sink_protocol_event` 错误事件 | RX (TX_TIMEOUT→TX) | `OBSERVER_ERROR` | 协议层错误 |

### 5.3 格式化输出（usbpd_observer_service）

```
每条记录:
    [USBPD][timestamp_ms][TX/RX][SOP][消息类型名] id=X ndo=X rev=X
    
    扩展消息头 → 追加 " size=X chunk=X[ request]"
    Request / EPR_REQUEST (且 1 DO) → 追加 " pdo=X raw_rdo=XXXXXXXX"
    Source_Cap / Sink_Cap (非扩展) → 逐个 PDO:
        Fixed: " pdo1=fixed:5000mV/900mA"
        Battery: " pdo2=battery:25000mW"
        Variable: " pdo3=variable:1500mA"
        APDO: " pdo4=apdo:X"
    
    追加原始字节十六进制串: " 00 11 22 33 ..."
```

**消息类型名映射**（`observer_message_name`）：
- 三分支：控制消息（`data_objects==0`）→ 数据消息（`extended==0`）→ 扩展消息（`extended==1`）
- 每个分支内 switch type 映射字符串

---

## 6. Sink 策略层（pd_sink）

### 6.1 状态机迁移表

| 当前状态 | 触发条件 | 动作 | 下一状态 |
|----------|----------|------|----------|
| UNATTACHED | CC1 或 CC2 检测到 Rp | — | ATTACH_WAIT, deadline = now + 100ms |
| ATTACH_WAIT | Rp 消失 | — | UNATTACHED |
| ATTACH_WAIT | deadline 到期，Rp 仍存在 | set_cc(选中 CC), configure(REV30/SINK/UFP), 上报 ATTACHED, 若 source_valid 则 sink_begin_selection | DISCOVERY, deadline = now + T_SINK_WAIT_CAP_MIN |
| DISCOVERY | wait_pending 且未到期 | — | 保持 |
| DISCOVERY | source_valid != 0 | wait_pending=0, sink_begin_selection | 由 begin_selection 决定 |
| DISCOVERY | deadline 到期且 get_source_cap_sent==0 | 发送 GET_SOURCE_CAP, sent=1, deadline = now + WAIT_CAP | 保持, deadline 重置 |
| DISCOVERY | deadline 到期且已发过 | — | SOFT_RESET |
| EPR_CAPS | epr_caps_valid != 0 | sink_begin_selection | 由 begin_selection 决定 |
| EPR_CAPS | deadline 到期, 未发过 EPR_GET_SOURCE_CAP | 发送 EPR_GET_SOURCE_CAP 扩展控制消息, sent=1 | 保持 |
| EPR_CAPS | deadline 到期, 已发过 | — | HARD_RESET |
| EPR_ENTER | epr_mode_sent==0 | 发送 EPR_Mode(Enter), operational_pdp = sink_cap_ext 的 epr_sink_operational_pdp_w 或 max_power/1000 | 保持, deadline = now + SENDER_RESPONSE_MAX |
| EPR_ENTER | deadline 到期 | — | SOFT_RESET |
| EPR_ENTER | 收到 EPR_Mode(Enter_Ack) | epr_enter_acked = 1, deadline 重置 | 保持 |
| EPR_ENTER | 收到 EPR_Mode(Enter_Succeeded) 且 acked==1 | status.epr_active=1, 上报 EPR_ENTERED, epr_caps_valid=0 | EPR_CAPS |
| EPR_ENTER | 收到 EPR_Mode(Enter_Failed) | epr_target_pending=0, fallback_active=1 | READY |
| EPR_ENTER | 收到 EPR_Mode(Exit) | status.epr_active=0, 上报 EPR_EXITED, 若 negotiated>20V → HARD_RESET 否则 READY | 由 negotiated 电压决定 |
| REQUESTED / TRANSITION | deadline 到期 | — | SOFT_RESET |
| READY | epr_exit_pending != 0 | 发送 EPR_Mode(Exit), 成功后 epr_exit_pending=0, epr_active=0, 上报 EPR_EXITED | 保持 READY |
| READY | epr_keepalive_pending 且 deadline 到期 | — | HARD_RESET |
| READY | target_dirty != 0 | sink_begin_selection | 由 begin_selection 决定 |
| READY | epr_active 且 keepalive 到期 | 发送 EPR KeepAlive 扩展控制消息, keepalive_pending=1, deadline = now + SENDER_RESPONSE | 保持 |
| READY | PPS 模式且 T_PPS_REQUEST_MS 到期 | sink_send_request（重新发送 Request 更新电压电流） | 保持 READY |
| SOFT_RESET | peer_soft_reset 且发送 ACCEPT 成功 | peer_soft_reset=0 | contract_valid? READY : DISCOVERY |
| SOFT_RESET | !peer_soft_reset 且 !soft_reset_sent | 发送 SOFT_RESET, sent=1, deadline = now + SENDER_RESPONSE_MAX | 保持 |
| SOFT_RESET | soft_reset_sent 且 deadline 到期 | — | HARD_RESET |
| HARD_RESET | hard_reset_count < N_HARD_RESET_COUNT | 发送 HardReset, count++ | 保持 HARD_RESET |
| HARD_RESET | count ≥ N_HARD_RESET_COUNT | — | ERROR_RECOVERY, deadline = now + 35ms |
| HARD_RESET | hard_reset_received (对端也发了 HardReset) | 同上 | 同上 |
| ERROR_RECOVERY | deadline 到期 | 复位 source_valid / epr_caps_valid / get_source_cap_sent | UNATTACHED |

### 6.2 sink_begin_selection 完整逻辑

```
sink_begin_selection(now_ms):
    target_dirty = 0
    fallback_active = 0
    
    requested_voltage > 20000:  /* EPR 目标 */
        contract_valid == 0:    /* 必须先建立 SPR 合约 */
            epr_target_pending = 1
            sink_choose_5v → sink_send_request → REQUESTED
        epr_active == 0:        /* 尚未进入 EPR */
            检查 source_pdo[0].fixed.epr_capable
            → PD_SINK_EPR_ENTER, deadline = now + T_ENTER_EPR_MS
        epr_caps_valid == 0:    /* 需先获取 EPR PDO */
            → PD_SINK_EPR_CAPS, deadline = now + SENDER_RESPONSE
        sink_choose_from(epr_source_pdo, epr=1) → sink_send_request
    
    epr_active != 0:  /* EPR 中但请求 ≤ 20V，需先 Exit 再协商 SPR */
        sink_choose_from(epr_source_pdo, epr=1)
        epr_exit_pending = 1 → sink_send_request (发 EPR_REQUEST)
    
    epr_active == 0 && requested ≤ 20V:  /* 常规 SPR */
        sink_choose_from(source_pdo, epr=0) → sink_send_request
    
    兜底: sink_choose_5v → sink_send_request
          仍失败 → PD_SINK_ERROR_RECOVERY
```

### 6.3 PDO 选择逻辑（sink_choose_from）

对每类 PDO 逐一匹配：

| PDO 类型 | 匹配条件 | RDO 分支 | 步长对齐 |
|----------|----------|----------|----------|
| Fixed | 电压精确相等, 电流 ≤ PDO 电流 | rdo.fixed | — |
| Variable | 电压 ∈ [min, max], 电流 ≤ PDO 电流 | rdo.fixed（同 Fixed） | — |
| Battery | 电压 ∈ [min, max], 功率 ≤ PDO 功率 | rdo.battery | — |
| PPS APDO | features.pps=1, 电压 ∈ [min, max], 电流 ≤ PDO 电流 | rdo.pps | 电压 20mV, 电流 50mA |
| SPR AVS | features.avs=1, 电压 9000~20000mV, 电流由电压决定（≤15V 取 current_15v, 否则 current_20v） | rdo.avs | 电压 25mV, 电流 50mA |
| EPR AVS | epr=1, 电压 ∈ [min, max], 电流 ≤ PDP/电压 | rdo.avs | 电压 25mV, 电流 50mA |

### 6.4 EPR 完整流程

```
Sink 侧:
    目标电压 > 20000
        │
        ├─ (无合约) 先发 5V Request → Accept → PS_RDY → READY(target_dirty=1)
        │
        └─ 有合约, epr_active=0
            │
            ├─ 检查 source_pdo[0].fixed.epr_capable==1
            │
            ├─ PD_SINK_EPR_ENTER: 发 EPR_Mode(Enter), PDP 由 sink_cap_ext 或 max_power 决定
            │   收到 Enter_Ack → epr_enter_acked=1, deadline 重置
            │   收到 Enter_Succeeded(且 acked) → epr_active=1 → PD_SINK_EPR_CAPS
            │   收到 Enter_Failed → fallback_active=1 → READY
            │
            ├─ PD_SINK_EPR_CAPS: 发 EPR_GET_SOURCE_CAP 扩展控制消息
            │   收到 EPR_SOURCE_CAP → epr_caps_valid=1 → sink_begin_selection
            │
            └─ sink_choose_from(epr_source_pdo, epr=1) → sink_send_request(EPR_REQUEST)
               Accept → PS_RDY → READY(epr_active=1)

EPR KeepAlive:
    READY 状态且 epr_active=1, keepalive 到期
        → 发 EPR KeepAlive 扩展控制消息, keepalive_pending=1
        收到 KeepAlive_ACK → keepalive_pending=0, keepalive_ms 重置
        keepalive_pending 时收到 NOT_SUPPORTED → HARD_RESET
        keepalive_pending 时 deadline 到期 → HARD_RESET

退出 EPR:
    READY 状态, 目标电压 ≤ 20V 或请求 epr_exit
        → 先协商 SPR (发 Request), 成功后 READY(epr_exit_pending=1)
        → 再发 EPR_Mode(Exit), 成功后 epr_active=0, epr_caps_valid=0
        Exit 后 negotiated_voltage > 20V → HARD_RESET
```

---

## 7. Source 策略层（pd_source）

### 7.1 状态机迁移表

| 当前状态 | 触发条件 | 动作 | 下一状态 |
|----------|----------|------|----------|
| UNATTACHED | CC1/CC2 检测到 Rd | — | ATTACH_WAIT, deadline = now + 100ms |
| ATTACH_WAIT | Rd 消失 | — | UNATTACHED |
| ATTACH_WAIT | deadline 到期, Rd 存在 | set_cc(选中 CC), DPM set_source(5V, 默认电流) 成功 | STARTUP, deadline = now + T_PS_TRANSITION_SPR_MAX |
| ATTACH_WAIT | set_source 失败 | — | ERROR_RECOVERY |
| STARTUP | source_ready==NULL 或 5V 就绪 | cap_attempts=0 | SEND_CAPABILITIES |
| STARTUP | deadline 到期 | — | ERROR_RECOVERY |
| SEND_CAPABILITIES | 发送 SOURCE_CAP 成功 | cap_attempts++ | WAIT_REQUEST, deadline = now + 150ms |
| WAIT_REQUEST | deadline 到期 | cap_attempts < 6 → SEND_CAPABILITIES, 否则 → ERROR_RECOVERY | — |
| WAIT_REQUEST / READY | 收到 Request | source_request_valid → PENDING_REPLY=ACCEPT, 否则=REJECT | 由 source_service_reply 触发 |
| — | — | Accept 发送成功, transition_started=0, accept_acked=0 | TRANSITION, deadline = now + T_PS_TRANSITION_SPR_MAX |
| TRANSITION | accept_acked==0 | 等待 GoodCRC | 保持 |
| TRANSITION | accept_acked==1 且 transition_started==0 | DPM set_source(目标电压电流), 成功→started=1 | 保持 |
| TRANSITION | source_ready | 发 PS_RDY, 记录 negotiated_voltage/current, PPS 检测, 上报 CONTRACT | READY |
| TRANSITION | deadline 到期 | — | HARD_RESET |
| READY | PPS 模式, PPS_TIMEOUT_MS 到期 | — | HARD_RESET |
| SOFT_RESET | peer_soft_reset 且发送 ACCEPT 成功 | peer_soft_reset=0 | SEND_CAPABILITIES |
| SOFT_RESET | !peer_soft_reset 且 !soft_reset_sent | 发送 SOFT_RESET, sent=1 | 保持 |
| SOFT_RESET | soft_reset_sent 且 deadline 到期 | — | HARD_RESET |
| HARD_RESET | !hard_reset_received 且 count < N_HARD_RESET_COUNT | 发送 HardReset, count++ | 保持 |
| HARD_RESET | 复位操作: clear_contract, DPM set_source(0V), DPM set_discharge(1), protocol_reset | — | ERROR_RECOVERY, deadline = now + 35ms |
| ERROR_RECOVERY | deadline 到期 | DPM set_discharge(0), protocol_reset | UNATTACHED |

### 7.2 Request 校验逻辑（source_request_valid）

```
Fixed / Variable PDO:
    current_ma = rdo.operating_current × 10
    校验: current_ma > 0 && ≤ PDO 电流
         max_current ≥ operating_current × 10
         max_current × 10 ≤ PDO 电流
    voltage_mv = Fixed: PDO 电压; Variable: PDO max 电压

Battery PDO:
    power_mw = rdo.operating_power × 250
    校验: power > 0 && ≤ PDO 功率
         max_power ≥ operating_power × 250
         max_power ≤ PDO 功率
    voltage_mv = PDO max 电压
    current_ma = power × 1000 / voltage

PPS APDO (features.pps=1):
    voltage_mv = rdo.output_voltage × 20
    current_ma = rdo.operating_current × 50
    校验: voltage ∈ [PDO min×100, PDO max×100]
         current > 0 && ≤ PDO 电流

SPR AVS (features.avs=1):
    voltage_mv = rdo.output_voltage × 25
    current_ma = rdo.operating_current × 50
    max_current = voltage ≤ 15V ? PDO.current_15v×10 : PDO.current_20v×10
    校验: voltage ∈ [9000, 20000]
         current > 0 && ≤ max_current
```

### 7.3 Transition 状态电源转换流程

```
WAIT_REQUEST → 收到 Request → PENDING_REPLY=ACCEPT
    │
    source_service_reply 发送 ACCEPT 成功:
        transition_started = 0
        accept_acked = 0
        → TRANSITION, deadline = now + T_PS_TRANSITION_SPR_MAX
    │
    TRANSITION 状态内:
        accept_acked==0 → 等待 Accept 的 GoodCRC (protocol_handle_rx 匹配后置位)
        accept_acked==1 && transition_started==0:
            DPM set_source(enable=1, requested_voltage, requested_current)
            成功 → transition_started = 1
            失败 → HARD_RESET
        transition_started==1 && source_ready==1:
            发 PS_RDY 控制消息
            记录 negotiated_voltage/current/selected_pdo/contract_valid
            检查 PDO 类型是否 PPS → status.pps_active
            pps_deadline = now + 13500ms
            上报 DPM_CONTRACT
            → READY
        deadline 到期 → HARD_RESET
```

### 7.4 Source 配置校验（source_config_valid）

| 校验点 | 规则 |
|--------|------|
| source_pdo_count | 非零且 ≤ USBPD_MAX_DATA_OBJ |
| sink_pdo_count | ≤ USBPD_MAX_DATA_OBJ |
| dpm.set_source | 必须非 NULL |
| source_pdo[0] | 必须 Fixed 且 5V（voltage_50mv==100） |
| Fixed PDO | epr_capable 必须为 0（Source 不宣告 EPR Fixed PDO） |
| EPR AVS PDO | 禁止出现在 source_pdo 列表 |
| PPS APDO | features.pps 必须为 1 |
| SPR AVS | features.avs 必须为 1 |
| sink_pdo[0] | 若 sink_pdo_count>0 则必须 Fixed 且 5V |

---

## 8. PDO / RDO 类型体系

### 8.1 PDO 类型速查

| type | subtype | union 分支 | 电压步长 | 电流/功率步长 | Sink 宣告 | Source 宣告 | EPR |
|------|---------|------------|----------|---------------|-----------|-------------|-----|
| Fixed | — | fixed | 50mV | 10mA | ✓ | ✓ | ✗ (epr_capable=0) |
| Variable | — | variable | 50mV (min/max) | 10mA | ✓ | ✓ | ✗ |
| Battery | — | battery | 50mV (min/max) | 250mW | ✓ | ✓ | ✗ |
| APDO | PPS | pps | 100mV (min/max) | 50mA | ✓ (features.pps=1) | ✓ (features.pps=1) | ✗ |
| APDO | SPR AVS | spr_avs | 100mV (min/max) | 10mA/10mA (≤15V/>15V) | ✓ (features.avs=1) | ✓ (features.avs=1) | ✗ |
| APDO | EPR AVS | avs | 100mV (min/max) | PDP/电压 | ✗ (SINK 不宣告) | ✗ (SOURCE 不宣告) | ✓ (仅 EPR PDO 列表) |

### 8.2 RDO 构造分支

| Sink 选择类型 | RDO union 分支 | 关键字段 |
|---------------|----------------|----------|
| PD_SELECT_FIXED | rdo.fixed | object_position / operating_current / max_current |
| PD_SELECT_VARIABLE | rdo.fixed（同 Fixed） | 同上 |
| PD_SELECT_BATTERY | rdo.battery | object_position / operating_power / max_power |
| PD_SELECT_PPS | rdo.pps | object_position / output_voltage / operating_current |
| PD_SELECT_AVS | rdo.avs | object_position / output_voltage / operating_current |

所有 RDO 分支共有字段：epr_capable / unchunked / no_suspend / usb_comm。

---

## 9. 错误恢复路径

### 9.1 RX 溢出

```
usbpd_protocol_task:
    usbpd_phy_take_rx_overflow() != 0
        → usbpd_phy_reset_rx()
        → usbpd_protocol_reset()
        → protocol_emit(USBPD_PROTOCOL_RX_OVERFLOW)
        return
    /* PHY 层溢出时丢弃最旧帧并置 rx_overflow=1，协议层检测后全复位 */
```

### 9.2 PHY 层错误

```
usbpd_protocol_task:
    usbpd_phy_receive 返回非 OK/BUSY
        → protocol_emit(USBPD_PROTOCOL_ERROR)

协议层收到 ERROR/TX_TIMEOUT/RX_OVERFLOW:
    Sink: reset_contract → HARD_RESET
    Source: clear_contract → 发 HardReset → ERROR_RECOVERY
```

### 9.3 EPR 退出后电压越界

```
Source 发 EPR_Mode(Exit) 后:
    若 negotiated_voltage > 20000mV → HARD_RESET
    （防止 EPR Exit 后仍保持 >20V 的非法电压）
```

### 9.4 Send_Capabilities 重试耗尽

```
Source WAIT_REQUEST deadline × PD_SOURCE_MAX_CAP_ATTEMPTS(6)
    → ERROR_RECOVERY
```

---

## 10. 关键时序交互图

### 10.1 首次连接（Sink 视角）

```
  PHY IRQ                  PHY task            协议层 task         Sink task          应用层
    │                        │                    │                  │                  │
    │ CC1=Rp → IRQ          │                    │                  │                  │
    │                        │                    │                  │                  │
    │                        │                    │                  │ATTACH_WAIT       │
    │                        │                    │                  │(100ms)           │
    │                        │                    │                  │                  │
    │                        │                    │                  │DISCOVERY         │
    │                        │                    │                  │ → GET_SOURCE_CAP │
    │                        │                    │                  │                  │
    │                        │                    │  SOURCE_CAP      │                  │
    │                        │                    │  → protocol_emit │                  │
    │                        │                    │                  │                  │
    │                        │                    │                  │sink_begin_select │
    │                        │                    │                  │ → Request (5V)   │
    │                        │                    │                  │                  │
    │                        │                    │  ACCEPT          │                  │
    │                        │                    │  → emit          │                  │
    │                        │                    │                  │TRANSITION        │
    │                        │                    │                  │                  │
    │                        │                    │  PS_RDY          │                  │
    │                        │                    │  → emit          │                  │
    │                        │                    │                  │READY             │
    │                        │                    │                  │                  │CONTRACT
    │                        │                    │                  │                  │(5V/1A)
```

### 10.2 GoodCRC 自动应答 vs 协议层重发

```
  PHY IRQ                                    协议层
    │                                          │
    │ 收到 DATA_REQUEST (非 GoodCRC)           │
    │                                          │
    │ device_send_goodcrc(sop, raw)            │
    │   构造 GoodCRC 头                        │
    │   device_start_tx → DMA 发送              │
    │   tx_busy=1                              │
    │   goodcrc_trace 入队                     │
    │                                          │
    │ TX_END 中断 → device_finish_tx           │
    │   tx_busy=0                              │
    │                                          │ protocol_send_frame(Request)
    │                                          │   tx_busy→PHY→返回OK
    │                                          │   awaiting_crc=1
    │                                          │
    │ (稍后) PHY task 刷写 goodcrc_trace       │
    │   → observer_record(GOODCRC)             │
    │                                          │ protocol_handle_rx(GOODCRC)
    │                                          │   sop/id 匹配
    │                                          │   awaiting_crc=0
    │                                          │   tx_id++
    │                                          │   emit TX_GOODCRC
    │                                          │
    │ ── 若 GoodCRC 超时 ──                    │
    │                                          │ deadline 到期
    │                                          │ usbpd_phy_send(tx_raw) 重发
    │                                          │ retry_count++
    │                                          │ 连续 3 次 → emit TX_TIMEOUT
```

---

## 附录：枚举常量引用

| 枚举 | 定义位置 | 关键值 |
|------|----------|--------|
| `usbpd_sop_e` | usbpd_def.h | SOP=0, SOP_PRIME=1, SOP_DPRIME=2, INVALID=3, HARD_RESET=4, CABLE_RESET=5 |
| `usbpd_cc_e` | usbpd_def.h | OPEN=0, RA=1, RD=2, RP_DEF=3, RP_1A5=4, RP_3A=5 |
| `usbpd_ctrl_e` | usbpd_def.h | GOODCRC=1, ACCEPT=2, REJECT=3, PS_RDY=8, WAIT=11, SOFT_RESET=12, HARD_RESET 在 sop 层 |
| `usbpd_data_e` | usbpd_def.h | SOURCE_CAP=1, REQUEST=2, SINK_CAP=4, EPR_REQUEST=10, EPR_MODE=11 |
| `usbpd_extended_e` | usbpd_def.h | SOURCE_CAP=1, STATUS=2, EPR_SOURCE_CAP=19, EPR_SINK_CAP=20 |
| `usbpd_apdo_subtype_e` | usbpd_def.h | PPS=0x00, SPR_AVS=0x01, EPR_AVS=0x02 |
| `usbpd_pps_timeout_ms` | pd_source.c | Source 侧 PPS 动态请求超时 13500ms |
| `USBPD_N_HARD_RESET_COUNT` | usbpd_def.h | HardReset 重发上限（通常 2） |
| `USBPD_N_RETRY_COUNT` | usbpd_def.h | GoodCRC 重发上限（通常 3） |
