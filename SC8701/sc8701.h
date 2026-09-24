/**
 * @file sc8701.h
 * @brief Generic SC8701 synchronous buck-boost controller driver.
 */

#ifndef SC8701_H
#define SC8701_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SC8701_VFB_REF_MV          1220U
#define SC8701_ILIM_REF_MV         1210U
#define SC8701_ILIM_GAIN_NUM       10U
#define SC8701_VIN_MIN_MV          2700U
#define SC8701_VIN_MAX_MV          36000U
#define SC8701_VOUT_MIN_MV         2000U
#define SC8701_VOUT_MAX_MV         36000U
#define SC8701_PWM_FREQ_MIN_HZ     20000U
#define SC8701_PWM_FREQ_MAX_HZ     100000U
#define SC8701_SOFT_START_MS_MAX   15U

typedef enum {
    SC8701_OK = 0,
    SC8701_ERR_NULL = -1,
    SC8701_ERR_PARAM = -2,
    SC8701_ERR_RANGE = -3,
    SC8701_ERR_HW = -4,
    SC8701_ERR_PG_TIMEOUT = -5,
    SC8701_ERR_FAULT = -6,
} sc8701_err_t;

typedef enum {
    SC8701_MODE_BUCK = 0,
    SC8701_MODE_BOOST = 1,
    SC8701_MODE_BUCK_BOOST = 2,
    SC8701_MODE_OFF = 3,
    SC8701_MODE_FAULT = 4,
} sc8701_mode_t;

typedef enum {
    SC8701_ITUNE_INPUT = 0,
    SC8701_ITUNE_OUTPUT = 1,
} sc8701_itune_target_t;

typedef enum {
    SC8701_ADC_VIN = 0,
    SC8701_ADC_VOUT = 1,
    SC8701_ADC_IIN = 2,
    SC8701_ADC_IOUT = 3,
    SC8701_ADC_VCC = 4,
    SC8701_ADC_TEMP = 5,
    SC8701_ADC_CH_MAX = 6,
} sc8701_adc_channel_t;

typedef void (*sc8701_gpio_set_t)(void *user, uint8_t level);
typedef uint8_t (*sc8701_gpio_get_t)(void *user);
typedef int8_t (*sc8701_pwm_init_t)(void *user, uint32_t freq_hz);
typedef void (*sc8701_pwm_set_t)(void *user, float duty);
typedef int8_t (*sc8701_adc_read_mv_t)(void *user, uint8_t channel, uint16_t *mv);
typedef void (*sc8701_delay_ms_t)(void *user, uint32_t ms);
typedef uint32_t (*sc8701_get_ms_t)(void *user);

typedef struct {
    sc8701_gpio_set_t ce_set;
    sc8701_gpio_set_t itune_set;
    sc8701_gpio_get_t pg_get;
    sc8701_pwm_init_t pwm_vout_init;
    sc8701_pwm_set_t pwm_vout_set;
    sc8701_pwm_init_t pwm_ilim_init;
    sc8701_pwm_set_t pwm_ilim_set;
    sc8701_adc_read_mv_t adc_read_mv;
    sc8701_delay_ms_t delay_ms;
    sc8701_get_ms_t get_ms;
} sc8701_hal_t;

typedef struct {
    uint32_t r_up_ohm;
    uint32_t r_down_ohm;
    uint32_t rilim1_ohm;
    uint32_t rilim2_ohm;
    uint32_t rsns1_uohm;
    uint32_t rsns2_uohm;
    uint32_t pwm_vout_freq_hz;
    uint32_t pwm_ilim_freq_hz;
} sc8701_hw_config_t;

typedef struct {
    const sc8701_hal_t *hal;
    void *user;
    sc8701_hw_config_t hw;

    uint32_t vout_set_mv;
    uint32_t iin_lim_ma;
    uint32_t iout_lim_ma;
    uint32_t vout_target_mv;
    uint32_t ilim_target_ma;
    uint32_t enable_time_ms;

    sc8701_itune_target_t itune_target;
    uint8_t enabled;
    uint8_t pg_ok;
    uint8_t fault;
} sc8701_ctx_t;

int8_t sc8701_init(sc8701_ctx_t *ctx,
                   const sc8701_hal_t *hal,
                   void *user,
                   const sc8701_hw_config_t *hw);
int8_t sc8701_enable(sc8701_ctx_t *ctx, uint32_t timeout_ms);
int8_t sc8701_disable(sc8701_ctx_t *ctx);
int8_t sc8701_set_vout(sc8701_ctx_t *ctx, uint32_t target_mv);
int8_t sc8701_set_ilim(sc8701_ctx_t *ctx, uint32_t target_ma);
int8_t sc8701_set_itune_target(sc8701_ctx_t *ctx, sc8701_itune_target_t target);
int8_t sc8701_clear_fault(sc8701_ctx_t *ctx);

uint8_t sc8701_get_pg(sc8701_ctx_t *ctx);
sc8701_mode_t sc8701_get_mode(sc8701_ctx_t *ctx);

uint8_t sc8701_is_enabled(const sc8701_ctx_t *ctx);
uint8_t sc8701_get_fault(const sc8701_ctx_t *ctx);
uint32_t sc8701_get_vout_set_mv(const sc8701_ctx_t *ctx);
uint32_t sc8701_get_vout_target_mv(const sc8701_ctx_t *ctx);
uint32_t sc8701_get_iin_limit_ma(const sc8701_ctx_t *ctx);
uint32_t sc8701_get_iout_limit_ma(const sc8701_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif /* SC8701_H */
