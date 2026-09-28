/**
* @file test_ws2812.c
* @brief WS2812 管理层 + SPI/PWM 后端单元测试
*
* 所属模块：WS2812 单元测试（mock PHY 层验证管理层与后端逻辑）。
* 对应被测代码：ws2812.c + ws2812_spi.c + ws2812_pwm.c。
*
* Mock 方式：
*   - 定义 mock_spi_phy_t / mock_pwm_phy_t 结构体，记录 init/transmit 调用次数与参数；
*   - 覆盖 ws2812_spi_phy_init / ws2812_spi_phy_transmit / ws2812_pwm_phy_init / ws2812_pwm_phy_transmit；
*   - synchronous=1 时 transmit 内立即调 ws2812_transfer_complete（模拟 DMA 同步完成）。
*
* 测试用例覆盖：
*   1. SPI 异步传输 + 核心 API（init/show/busy/complete/set_pixel/fill/clear）；
*   2. 全部 6 种颜色顺序的编码正确性；
*   3. PWM 编码时序（T0H=28 tick, T1H=60 tick）；
*   4. 参数校验与 IO 错误传播（init_result/transmit_result）。
*/
#include "ws2812.h"
#include "ws2812_pwm.h"
#include "ws2812_spi.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* ===== Mock 结构体 ===== */

/** @brief SPI PHY mock：记录 init/transmit 调用参数 */
typedef struct
{
    uint32_t target_hz;              /**< 记录 init 时的时钟频率 */
    const uint8_t *data;            /**< 记录 transmit 的数据指针 */
    size_t length;                   /**< 记录 transmit 的长度 */
    ws2812_t *owner;                 /**< 记录 transmit 的 owner（complete 回调目标） */
    int init_result;                 /**< 模拟 init 返回值（0=成功） */
    int transmit_result;             /**< 模拟 transmit 返回值（0=成功） */
    uint8_t synchronous;             /**< 1=transmit 内同步调 transfer_complete */
    unsigned init_calls;             /**< init 被调用次数 */
    unsigned transmit_calls;         /**< transmit 被调用次数 */
} mock_spi_phy_t;

/** @brief PWM PHY mock：记录 init/transmit 调用参数 */
typedef struct
{
    uint16_t period_ticks;           /**< 记录 init 时的 PWM 周期 */
    const uint16_t *data;            /**< 记录 transmit 的数据指针 */
    size_t count;                    /**< 记录 transmit 的数据项数 */
    ws2812_t *owner;                 /**< 记录 transmit 的 owner */
    int init_result;                 /**< 模拟 init 返回值 */
    int transmit_result;             /**< 模拟 transmit 返回值 */
    uint8_t synchronous;             /**< 1=transmit 内同步调 transfer_complete */
    unsigned init_calls;             /**< init 被调用次数 */
    unsigned transmit_calls;         /**< transmit 被调用次数 */
} mock_pwm_phy_t;

/* ===== 弱符号覆盖：SPI PHY ===== */

int ws2812_spi_phy_init(void *user, uint32_t target_hz)
{
    mock_spi_phy_t *mock = (mock_spi_phy_t *)user;
    ++mock->init_calls;
    mock->target_hz = target_hz;
    return mock->init_result;
}

int ws2812_spi_phy_transmit(void *user, const uint8_t *data, size_t length, ws2812_t *owner)
{
    mock_spi_phy_t *mock = (mock_spi_phy_t *)user;
    ++mock->transmit_calls;
    mock->data = data;
    mock->length = length;
    if (mock->transmit_result != 0)
    {
        return mock->transmit_result;
    }
    mock->owner = owner;
    if (mock->synchronous != 0U)
    {
        ws2812_transfer_complete(owner);
    }
    return 0;
}

/* ===== 弱符号覆盖：PWM PHY ===== */

int ws2812_pwm_phy_init(void *user, uint16_t period_ticks)
{
    mock_pwm_phy_t *mock = (mock_pwm_phy_t *)user;
    ++mock->init_calls;
    mock->period_ticks = period_ticks;
    return mock->init_result;
}

int ws2812_pwm_phy_transmit(void *user, const uint16_t *data, size_t count, ws2812_t *owner)
{
    mock_pwm_phy_t *mock = (mock_pwm_phy_t *)user;
    ++mock->transmit_calls;
    mock->data = data;
    mock->count = count;
    if (mock->transmit_result != 0)
    {
        return mock->transmit_result;
    }
    mock->owner = owner;
    if (mock->synchronous != 0U)
    {
        ws2812_transfer_complete(owner);
    }
    return 0;
}

/* ===== 测试辅助：SPI 编码参考实现（与 ws2812_spi.c 的 encode_channel 一致） ===== */

static void encode_byte(uint8_t output[3], uint8_t value)
{
    uint32_t encoded = 0U;
    uint8_t bit;
    for (bit = 0U; bit < 8U; ++bit)
    {
        encoded = (encoded << 3U) | (((value & (uint8_t)(0x80U >> bit)) != 0U) ? 6U : 4U);
    }
    output[0] = (uint8_t)(encoded >> 16U);
    output[1] = (uint8_t)(encoded >> 8U);
    output[2] = (uint8_t)encoded;
}

/* ===== 测试用例 ===== */

/**
 * @brief  测试 1：SPI 异步传输 + 核心 API
 *
 * 覆盖：required_buffer_size / configure / init / set_pixel / fill_hex / show / busy /
 *       take_complete / transfer_complete / clear / AND_SHOW 便捷接口。
 *
 * 编码验证：2 灯珠 GRB 顺序, fill 0x123456, set_pixel(0, FF, 00, AA)
 *   灯珠 0: G=0xFF(11111111→0xDB6DB6), R=0x00(00000000→0x924924), B=0xAA(10101010→0xD34D34)
 *   灯珠 1: G=0x34, R=0x12, B=0x56
 */
static void test_spi_async_and_core_api(void)
{
    ws2812_rgb_t pixels[2];
    uint8_t tx[WS2812_SPI_BUFFER_SIZE(2U, WS2812_SPI_DEFAULT_HZ, WS2812_DEFAULT_RESET_US)];
    mock_spi_phy_t mock = {0};
    ws2812_spi_t backend;
    ws2812_t strip;
    ws2812_spi_config_t spi_config = {
        &mock, tx, sizeof(tx), WS2812_SPI_DEFAULT_HZ, WS2812_DEFAULT_RESET_US,
    };
    ws2812_config_t config = {
        pixels, 2U, WS2812_ORDER_GRB, WS2812_BACKEND_SPI, &backend,
    };

    /* 缓冲大小计算校验 */
    assert(ws2812_spi_required_buffer_size(2U, WS2812_SPI_DEFAULT_HZ, WS2812_DEFAULT_RESET_US) == sizeof(tx));

    /* 后端配置 + 管理层初始化 */
    assert(ws2812_spi_configure(&backend, &spi_config, 2U) == WS2812_OK);
    assert(ws2812_init(&strip, &config) == WS2812_OK);
    assert(mock.init_calls == 1U);
    assert(mock.target_hz == WS2812_SPI_DEFAULT_HZ);
    /* init 内部 memset(pixels,0) */
    assert(pixels[0].r == 0U && pixels[1].b == 0U);

    /* set_pixel 参数校验：NULL / 越界 */
    assert(ws2812_set_pixel_rgb(NULL, 0U, 0U, 0U, 0U) == WS2812_RET_NULL);
    assert(ws2812_set_pixel_rgb(&strip, 2U, 0U, 0U, 0U) == WS2812_RET_RANGE);

    /* fill + set_pixel 组合 */
    assert(ws2812_fill_hex(&strip, 0x123456UL) == WS2812_OK);
    assert(pixels[1].r == 0x12U && pixels[1].g == 0x34U && pixels[1].b == 0x56U);
    assert(ws2812_set_pixel_rgb(&strip, 0U, 0xFFU, 0x00U, 0xAAU) == WS2812_OK);

    /* show → busy=1, SPI DMA 启动 */
    assert(ws2812_show(&strip) == WS2812_OK);
    assert(strip.busy == 1U);
    assert(mock.length == sizeof(tx));
    /* 灯珠 0 GRB 编码: G=0xFF→0xDB6DB6, R=0x00→0x924924, B=0xAA→0xD34D34 */
    assert(tx[0] == 0x92U && tx[1] == 0x49U && tx[2] == 0x24U);
    assert(tx[3] == 0xDBU && tx[4] == 0x6DU && tx[5] == 0xB6U);
    assert(tx[6] == 0xD3U && tx[7] == 0x4DU && tx[8] == 0x34U);
    /* RESET 序列全 0 */
    assert(tx[sizeof(tx) - 1U] == 0U);

    /* show 时忙 → 返回 BUSY */
    assert(ws2812_show(&strip) == WS2812_RET_BUSY);

    /* AND_SHOW 便捷接口：设置成功但 show 返回 BUSY → 整体返回 BUSY */
    assert(ws2812_set_pixel_hex_and_show(&strip, 0U, 0x010203UL) == WS2812_RET_BUSY);
    /* 但像素确实被修改了 */
    assert(pixels[0].r == 1U && pixels[0].g == 2U && pixels[0].b == 3U);

    /* 模拟 DMA 完成：transfer_complete → busy=0, take_complete=1 */
    ws2812_transfer_complete(mock.owner);
    assert(strip.busy == 0U);
    assert(ws2812_take_complete(&strip) == 1U);
    /* 重复 complete → take_complete=0（已清零） */
    ws2812_transfer_complete(mock.owner);
    assert(ws2812_take_complete(&strip) == 0U);

    /* clear → 全黑 */
    assert(ws2812_clear(&strip) == WS2812_OK);
    assert(pixels[0].r == 0U && pixels[1].g == 0U);
}

/**
 * @brief  测试 2：全部 6 种颜色顺序的编码正确性
 *
 * 对 GRB/RGB/RBG/GBR/BRG/BGR 六种顺序，
 * 输入 pixel=(R=0x80, G=0x40, B=0x20)，
 * 验证后端 encode_channel 输出的 SPI 字节是否符合预期通道映射。
 */
static void test_all_color_orders(void)
{
    static const ws2812_color_order_t orders[] = {
        WS2812_ORDER_GRB, WS2812_ORDER_RGB, WS2812_ORDER_RBG, WS2812_ORDER_GBR, WS2812_ORDER_BRG, WS2812_ORDER_BGR,
    };
    /* 每种顺序下三个通道的实际值（GRB→[G=0x40, R=0x80, B=0x20]） */
    static const uint8_t expected[][3] = {
        {0x40U, 0x80U, 0x20U}, {0x80U, 0x40U, 0x20U}, {0x80U, 0x20U, 0x40U},
        {0x40U, 0x20U, 0x80U}, {0x20U, 0x80U, 0x40U}, {0x20U, 0x40U, 0x80U},
    };
    size_t i;

    for (i = 0U; i < sizeof(orders) / sizeof(orders[0]); ++i)
    {
        ws2812_rgb_t pixel[1];
        uint8_t tx[WS2812_SPI_BUFFER_SIZE(1U, 2400000UL, 4UL)];
        uint8_t encoded[3];
        mock_spi_phy_t mock = {0};
        ws2812_spi_t backend;
        ws2812_t strip;
        ws2812_spi_config_t spi_config = {
            &mock, tx, sizeof(tx), 2400000UL, 4UL,
        };
        ws2812_config_t config = {
            pixel, 1U, orders[i], WS2812_BACKEND_SPI, &backend,
        };
        uint8_t channel;

        mock.synchronous = 1U;  /* transmit 内同步完成 */
        assert(ws2812_spi_configure(&backend, &spi_config, 1U) == WS2812_OK);
        assert(ws2812_init(&strip, &config) == WS2812_OK);
        assert(ws2812_set_pixel_rgb(&strip, 0U, 0x80U, 0x40U, 0x20U) == WS2812_OK);
        assert(ws2812_show(&strip) == WS2812_OK);
        assert(strip.busy == 0U);  /* synchronous → complete 已调 */

        /* 逐通道验证 SPI 编码字节 */
        for (channel = 0U; channel < 3U; ++channel)
        {
            encode_byte(encoded, expected[i][channel]);
            assert(memcmp(&tx[(size_t)channel * 3U], encoded, 3U) == 0);
        }
    }
}

/**
 * @brief  测试 3：PWM 编码时序
 *
 * PWM 后端参数：period=100 tick, T0H=28 tick, T1H=60 tick, reset=3 tick
 * 单灯珠编码：8bit × 3通道 × 3tick/bit = 72 tick
 *
 * 输入 pixel=(R=0x80, G=0x00, B=0x01)：
 *   R=0x80=10000000 → 第 0 位为 1 → 高电平 60 tick，其余 7 位 T0H=28 tick
 *   G=0x00=00000000 → 全部 28 tick
 *   B=0x01=00000001 → 前 7 位 28 tick, 最后 1 位 60 tick
 *   尾部 reset=0（3 tick 低电平）
 *
 * 期望：tx[0]=60, tx[1..22]=28, tx[23]=60, tx[24..26]=0
 */
static void test_pwm_encoding(void)
{
    ws2812_rgb_t pixel[1];
    uint16_t tx[WS2812_PWM_BUFFER_SIZE(1U, 3U)];
    mock_pwm_phy_t mock = {0};
    ws2812_pwm_t backend;
    ws2812_t strip;
    ws2812_pwm_config_t pwm_config = {
        &mock, tx, sizeof(tx) / sizeof(tx[0]), 100U, 28U, 60U, 3U,
    };
    ws2812_config_t config = {
        pixel, 1U, WS2812_ORDER_RGB, WS2812_BACKEND_PWM, &backend,
    };
    size_t i;

    assert(ws2812_pwm_required_buffer_count(1U, 3U) == 27U);
    assert(ws2812_pwm_configure(&backend, &pwm_config, 1U) == WS2812_OK);
    assert(ws2812_init(&strip, &config) == WS2812_OK);
    assert(mock.period_ticks == 100U);
    assert(ws2812_set_pixel_rgb(&strip, 0U, 0x80U, 0x00U, 0x01U) == WS2812_OK);
    assert(ws2812_show(&strip) == WS2812_OK);
    assert(mock.count == 27U);

    /* R: 0x80 → 第 0 位 1 → tx[0]=60 */
    assert(tx[0] == 60U);
    /* G: 0x00 → 全 0 → tx[1..22] 全 28 */
    for (i = 1U; i < 23U; ++i)
    {
        assert(tx[i] == 28U);
    }
    /* B: 0x01 → 最后 1 位 1 → tx[23]=60 */
    assert(tx[23] == 60U);
    /* 尾部 RESET: 3 tick 低电平 */
    assert(tx[24] == 0U && tx[25] == 0U && tx[26] == 0U);

    ws2812_transfer_complete(mock.owner);
    assert(strip.busy == 0U);
}

/**
 * @brief  测试 4：参数校验与 IO 错误传播
 *
 * 覆盖：configure NULL / buffer 不足 / init IO 失败 / show IO 失败 /
 *       PWM buffer 不足 / color_order 非法 / init NULL。
 */
static void test_validation_and_io_errors(void)
{
    ws2812_rgb_t pixel[1];
    uint8_t tx[WS2812_SPI_BUFFER_SIZE(1U, 2400000UL, 4UL)];
    mock_spi_phy_t mock = {0};
    ws2812_spi_t backend;
    ws2812_t strip;
    ws2812_spi_config_t spi_config = {
        &mock, tx, sizeof(tx), 2400000UL, 4UL,
    };
    uint16_t pwm_tx[25];
    mock_pwm_phy_t pwm_mock = {0};
    ws2812_pwm_t pwm_backend;
    ws2812_pwm_config_t pwm_config = {
        &pwm_mock, pwm_tx, 25U, 100U, 60U, 28U, 1U,
    };

    /* SPI configure: backend NULL → RET_NULL */
    assert(ws2812_spi_configure(NULL, &spi_config, 1U) == WS2812_RET_NULL);

    /* SPI configure: 缓冲不足 → RET_BUFFER */
    spi_config.tx_buffer_size = sizeof(tx) - 1U;
    assert(ws2812_spi_configure(&backend, &spi_config, 1U) == WS2812_RET_BUFFER);
    spi_config.tx_buffer_size = sizeof(tx);
    assert(ws2812_spi_configure(&backend, &spi_config, 1U) == WS2812_OK);

    /* init 内部调用 spi_phy_init，返回 -1 → RET_IO */
    mock.init_result = -1;
    assert(ws2812_init(&strip, &config) == WS2812_RET_IO);

    /* init 成功后，show 调用 phy_transmit 返回 -1 → RET_IO，busy 应清 0 */
    mock.init_result = 0;
    assert(ws2812_init(&strip, &config) == WS2812_OK);
    mock.transmit_result = -1;
    assert(ws2812_show(&strip) == WS2812_RET_IO);
    assert(strip.busy == 0U);

    /* PWM configure: T0H>T1H → RET_PARAM（时序约束） */
    assert(ws2812_pwm_configure(&pwm_backend, &pwm_config, 1U) == WS2812_RET_PARAM);
    pwm_config.t0h_ticks = 28U;
    pwm_config.t1h_ticks = 60U;
    /* PWM configure: 缓冲 25 < 27 → RET_BUFFER */
    pwm_config.tx_buffer_count = 24U;
    assert(ws2812_pwm_configure(&pwm_backend, &pwm_config, 1U) == WS2812_RET_BUFFER);

    /* init: color_order 非法 (>BGR) → RET_PARAM */
    config.color_order = (ws2812_color_order_t)99;
    assert(ws2812_init(&strip, &config) == WS2812_RET_PARAM);
    /* init: dev NULL → RET_NULL */
    assert(ws2812_init(NULL, &config) == WS2812_RET_NULL);
}

int main(void)
{
    test_spi_async_and_core_api();
    test_all_color_orders();
    test_pwm_encoding();
    test_validation_and_io_errors();
    return 0;
}
