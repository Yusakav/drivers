/**
 * @file ws2812_pwm.h
 * @brief WS2812 timer/PWM compare-value backend.
 */

#ifndef WS2812_PWM_H
#define WS2812_PWM_H

#include "ws2812.h"

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

#define WS2812_PWM_VALUES_PER_PIXEL 24UL
#define WS2812_PWM_BUFFER_SIZE(pixel_count, reset_slots)                                                               \
    ((size_t)(pixel_count) * WS2812_PWM_VALUES_PER_PIXEL + (size_t)(reset_slots))

    typedef struct
    {
        void *phy_user;
        uint16_t *tx_buffer;
        size_t tx_buffer_count;
        uint16_t period_ticks;
        uint16_t t0h_ticks;
        uint16_t t1h_ticks;
        uint16_t reset_slots;
    } ws2812_pwm_config_t;

    typedef struct
    {
        ws2812_pwm_config_t config;
        size_t frame_count;
        uint8_t initialized;
    } ws2812_pwm_t;

    /* Implement these fixed symbols in the selected platform PHY. */
    int ws2812_pwm_phy_init(void *user, uint16_t period_ticks);
    int ws2812_pwm_phy_transmit(void *user, const uint16_t *compare_values, size_t value_count, ws2812_t *owner);

    size_t ws2812_pwm_required_buffer_count(uint16_t pixel_count, uint16_t reset_slots);
    ws2812_ret_t ws2812_pwm_configure(ws2812_pwm_t *backend, const ws2812_pwm_config_t *config, uint16_t pixel_count);
    ws2812_ret_t ws2812_pwm_backend_init(ws2812_pwm_t *backend);
    ws2812_ret_t ws2812_pwm_backend_start(ws2812_pwm_t *backend, const ws2812_rgb_t *pixels, uint16_t pixel_count,
                                          ws2812_color_order_t order, ws2812_t *owner);

#ifdef __cplusplus
}
#endif

#endif /* WS2812_PWM_H */
