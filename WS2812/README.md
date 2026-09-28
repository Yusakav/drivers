# WS2812 通用驱动

驱动分为三层：`ws2812.c` 管理像素和发送状态，`SPI/`、`PWM/` 负责把像素编码成外设可发送的数据，PHY 负责具体 MCU 的 GPIO、外设和 DMA。

## 基本使用

应用必须静态提供两块内存：

- `ws2812_rgb_t pixels[LED_COUNT]`：当前准备显示的 RGB 像素；
- 后端发送缓冲：SPI 使用字节数组，PWM 使用 `uint16_t` 比较值数组。

先调用 `ws2812_spi_configure()` 或 `ws2812_pwm_configure()` 配置后端，再通过 `WS2812_BACKEND_SPI` 或 `WS2812_BACKEND_PWM` 把后端交给 `ws2812_init()`。公共层使用枚举分派，不使用函数指针。`ws2812_set_pixel_*()`、`ws2812_fill_*()` 和 `ws2812_clear()` 只修改像素缓冲；`ws2812_show()` 才编码并开始发送。

发送期间 `ws2812_show()` 返回 `WS2812_RET_BUSY`，但颜色设置接口仍可准备下一帧。PHY 完成整帧（包括复位低电平）后调用固定接口 `ws2812_transfer_complete(dev)`。应用使用 `ws2812_is_busy()` 查看状态，并使用 `ws2812_take_complete()` 读取并清除完成标志；驱动不注册或调用应用函数指针。

## SPI 后端

SPI 后端使用三位符号：WS2812 数据 0 编码为 `100`，数据 1 编码为 `110`，因此每颗 RGB 灯需要 9 字节。帧尾零字节数量为：

```text
ceil(spi_hz * reset_us / 8,000,000)
```

可使用 `WS2812_SPI_BUFFER_SIZE()` 计算静态缓冲大小。默认 SPI 频率为 2.4 MHz，默认复位低电平为 300 µs。具体器件版本的脉宽要求可能不同，应以所用灯珠的数据手册和逻辑分析仪测量结果为准。

SPI PHY 必须实现固定符号 `ws2812_spi_phy_init()` 和 `ws2812_spi_phy_transmit()`，连续发送完整缓冲，不得在像素数据中插入停顿；发送完尾部零字节后调用 `ws2812_transfer_complete(owner)`。若采用异步 DMA，完成前必须持续占用传入的发送缓冲。后端自带的弱默认实现只返回失败，链接平台 PHY 的同名强符号后会自动替换它；一个固件中每类后端只能选择一个 PHY 实现，多实例通过 `phy_user` 区分外设上下文。

CH582M PHY 固定使用重映射后的 SPI0 和 PB13/PB14，按 `GetSysClock()` 计算最接近目标频率的 8 位分频值。WCH SPI0 DMA 要求源地址四字节对齐且长度不超过 `UINT16_MAX`，PHY 会检查这两个条件。

## PWM 后端与 PHY 要求

PWM 缓冲的每个 `uint16_t` 元素对应一个 WS2812 数据周期：0 使用 `t0h_ticks`，1 使用 `t1h_ticks`，帧尾使用若干个零比较值产生复位低电平。容量使用 `WS2812_PWM_BUFFER_SIZE()` 计算。

PHY 应按目标器件的数据手册完成以下工作：

1. 将 PWM 周期配置为约 1.25 µs（800 kbit/s），并设置 `period_ticks` 对应的计数周期。
2. 使用 DMA 连续写入比较寄存器，不在 24 位像素数据之间产生停顿。
3. 完整发送所有零占空比复位槽，并在停止 PWM 后继续保持输出为低。
4. 实现固定符号 `ws2812_pwm_phy_init()` 和 `ws2812_pwm_phy_transmit()`，仅在 DMA、最后一个 PWM 周期和复位低电平均已完成后调用 `ws2812_transfer_complete(owner)`。
5. 在完成前不重新使用或修改驱动交付的比较值缓冲。

常见换算方式为：

```text
period_ticks = timer_clock_hz * 1.25 us
t0h_ticks    = timer_clock_hz * T0H
t1h_ticks    = timer_clock_hz * T1H
reset_slots  = ceil(reset_us / 1.25 us)
```

初始化会检查 `0 < t0h_ticks < t1h_ticks < period_ticks`，但脉宽是否满足特定 WS2812/兼容器件仍由平台集成者验证。

## 内存示例

一颗灯、默认 SPI 参数需要 `9 + 90 = 99` 字节发送缓冲。PWM 使用 240 个复位槽表示 300 µs 时，一颗灯需要 `(24 + 240) * 2 = 528` 字节发送缓冲。

驱动不使用堆内存，不提供亮度、Gamma、动画队列或并发保护。多个实例只要分别提供像素缓冲、后端上下文和发送缓冲即可独立工作。
