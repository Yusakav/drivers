/**
 * @file ws2812_pwm.c
 * @brief WS2812 timer/PWM compare-value backend.
 */

#include "ws2812_pwm.h"

#include <stdint.h>
#include <string.h>

#if defined(__GNUC__) || defined(__clang__)
#define WS2812_WEAK __attribute__((weak))
#else
#define WS2812_WEAK
#endif

WS2812_WEAK int ws2812_pwm_phy_init(void *user, uint16_t period_ticks)
{
    (void)user;
    (void)period_ticks;
    return -1;
}

WS2812_WEAK int ws2812_pwm_phy_transmit(void *user, const uint16_t *compare_values, size_t value_count, ws2812_t *owner)
{
    (void)user;
    (void)compare_values;
    (void)value_count;
    (void)owner;
    return -1;
}

static void ordered_channels(const ws2812_rgb_t *pixel, ws2812_color_order_t order, uint8_t channels[3])
{
    switch (order)
    {
    case WS2812_ORDER_RGB:
        channels[0] = pixel->r;
        channels[1] = pixel->g;
        channels[2] = pixel->b;
        break;
    case WS2812_ORDER_RBG:
        channels[0] = pixel->r;
        channels[1] = pixel->b;
        channels[2] = pixel->g;
        break;
    case WS2812_ORDER_GBR:
        channels[0] = pixel->g;
        channels[1] = pixel->b;
        channels[2] = pixel->r;
        break;
    case WS2812_ORDER_BRG:
        channels[0] = pixel->b;
        channels[1] = pixel->r;
        channels[2] = pixel->g;
        break;
    case WS2812_ORDER_BGR:
        channels[0] = pixel->b;
        channels[1] = pixel->g;
        channels[2] = pixel->r;
        break;
    case WS2812_ORDER_GRB:
    default:
        channels[0] = pixel->g;
        channels[1] = pixel->r;
        channels[2] = pixel->b;
        break;
    }
}

size_t ws2812_pwm_required_buffer_count(uint16_t pixel_count, uint16_t reset_slots)
{
    uint64_t total;

    if ((pixel_count == 0U) || (reset_slots == 0U))
    {
        return 0U;
    }
    total = ((uint64_t)pixel_count * WS2812_PWM_VALUES_PER_PIXEL) + reset_slots;
    return (total <= (uint64_t)SIZE_MAX) ? (size_t)total : 0U;
}

ws2812_ret_t ws2812_pwm_configure(ws2812_pwm_t *backend, const ws2812_pwm_config_t *config, uint16_t pixel_count)
{
    size_t required;

    if ((backend == NULL) || (config == NULL))
    {
        return WS2812_RET_NULL;
    }
    if (config->tx_buffer == NULL)
    {
        return WS2812_RET_PARAM;
    }
    if ((config->t0h_ticks == 0U) || (config->t0h_ticks >= config->t1h_ticks) ||
        (config->t1h_ticks >= config->period_ticks))
    {
        return WS2812_RET_PARAM;
    }
    required = ws2812_pwm_required_buffer_count(pixel_count, config->reset_slots);
    if (required == 0U)
    {
        return WS2812_RET_PARAM;
    }
    if (config->tx_buffer_count < required)
    {
        return WS2812_RET_BUFFER;
    }

    memset(backend, 0, sizeof(*backend));
    backend->config = *config;
    backend->frame_count = required;
    return WS2812_OK;
}

ws2812_ret_t ws2812_pwm_backend_init(ws2812_pwm_t *backend)
{
    if (backend == NULL)
    {
        return WS2812_RET_NULL;
    }
    if (backend->frame_count == 0U)
    {
        return WS2812_RET_PARAM;
    }
    if (ws2812_pwm_phy_init(backend->config.phy_user, backend->config.period_ticks) != 0)
    {
        return WS2812_RET_IO;
    }
    backend->initialized = 1U;
    return WS2812_OK;
}

ws2812_ret_t ws2812_pwm_backend_start(ws2812_pwm_t *backend, const ws2812_rgb_t *pixels, uint16_t pixel_count,
                                      ws2812_color_order_t order, ws2812_t *owner)
{
    size_t required;
    size_t offset = 0U;
    uint16_t pixel_index;
    uint8_t channel_index;
    uint8_t bit;

    if ((backend == NULL) || (pixels == NULL) || (owner == NULL))
    {
        return WS2812_RET_NULL;
    }
    if (backend->initialized == 0U)
    {
        return WS2812_RET_PARAM;
    }
    required = ws2812_pwm_required_buffer_count(pixel_count, backend->config.reset_slots);
    if ((required == 0U) || (required > backend->config.tx_buffer_count))
    {
        return WS2812_RET_BUFFER;
    }

    for (pixel_index = 0U; pixel_index < pixel_count; ++pixel_index)
    {
        uint8_t channels[3];
        ordered_channels(&pixels[pixel_index], order, channels);
        for (channel_index = 0U; channel_index < 3U; ++channel_index)
        {
            for (bit = 0U; bit < 8U; ++bit)
            {
                backend->config.tx_buffer[offset++] = ((channels[channel_index] & (uint8_t)(0x80U >> bit)) != 0U)
                                                          ? backend->config.t1h_ticks
                                                          : backend->config.t0h_ticks;
            }
        }
    }
    memset(&backend->config.tx_buffer[offset], 0, (required - offset) * sizeof(backend->config.tx_buffer[0]));

    if (ws2812_pwm_phy_transmit(backend->config.phy_user, backend->config.tx_buffer, required, owner) != 0)
    {
        return WS2812_RET_IO;
    }
    return WS2812_OK;
}

#undef WS2812_WEAK
