#include "sc8701.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ASSERT_TRUE(expr)                                                       \
    do {                                                                        \
        if (!(expr)) {                                                          \
            printf("ASSERT failed %s:%d: %s\n", __FILE__, __LINE__, #expr);    \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                         \
    do {                                                                        \
        int exp__ = (int)(expected);                                            \
        int act__ = (int)(actual);                                              \
        if (exp__ != act__) {                                                   \
            printf("ASSERT failed %s:%d: expected %d got %d\n",                \
                   __FILE__, __LINE__, exp__, act__);                           \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_EQ_U32(expected, actual)                                         \
    do {                                                                        \
        uint32_t exp__ = (uint32_t)(expected);                                  \
        uint32_t act__ = (uint32_t)(actual);                                    \
        if (exp__ != act__) {                                                   \
            printf("ASSERT failed %s:%d: expected %lu got %lu\n",              \
                   __FILE__, __LINE__,                                          \
                   (unsigned long)exp__, (unsigned long)act__);                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_NEAR_FLOAT(expected, actual, epsilon)                            \
    do {                                                                        \
        float exp__ = (float)(expected);                                        \
        float act__ = (float)(actual);                                          \
        float diff__ = (act__ > exp__) ? (act__ - exp__) : (exp__ - act__);     \
        if (diff__ > (float)(epsilon)) {                                        \
            printf("ASSERT failed %s:%d: expected %.6f got %.6f\n",            \
                   __FILE__, __LINE__, exp__, act__);                           \
            return 1;                                                           \
        }                                                                       \
    } while (0)

typedef struct {
    uint8_t ce_level;
    uint8_t itune_level;
    uint8_t pg_level;
    float vout_duty;
    float ilim_duty;
    uint32_t now_ms;
    uint32_t delay_total_ms;
    uint32_t vout_freq_hz;
    uint32_t ilim_freq_hz;
    uint16_t adc[SC8701_ADC_CH_MAX];
    int8_t fail_vout_init;
    int8_t fail_ilim_init;
} mock_hw_t;

static void mock_ce_set(void *user, uint8_t level)
{
    ((mock_hw_t *)user)->ce_level = level;
}

static void mock_itune_set(void *user, uint8_t level)
{
    ((mock_hw_t *)user)->itune_level = level;
}

static uint8_t mock_pg_get(void *user)
{
    return ((mock_hw_t *)user)->pg_level;
}

static int8_t mock_pwm_vout_init(void *user, uint32_t freq_hz)
{
    mock_hw_t *mock = (mock_hw_t *)user;
    mock->vout_freq_hz = freq_hz;
    return mock->fail_vout_init;
}

static int8_t mock_pwm_ilim_init(void *user, uint32_t freq_hz)
{
    mock_hw_t *mock = (mock_hw_t *)user;
    mock->ilim_freq_hz = freq_hz;
    return mock->fail_ilim_init;
}

static void mock_pwm_vout_set(void *user, float duty)
{
    ((mock_hw_t *)user)->vout_duty = duty;
}

static void mock_pwm_ilim_set(void *user, float duty)
{
    ((mock_hw_t *)user)->ilim_duty = duty;
}

static int8_t mock_adc_read_mv(void *user, uint8_t channel, uint16_t *mv)
{
    mock_hw_t *mock = (mock_hw_t *)user;

    if ((channel >= SC8701_ADC_CH_MAX) || (mv == NULL)) {
        return -1;
    }
    *mv = mock->adc[channel];
    return 0;
}

static void mock_delay_ms(void *user, uint32_t ms)
{
    mock_hw_t *mock = (mock_hw_t *)user;
    mock->now_ms += ms;
    mock->delay_total_ms += ms;
}

static uint32_t mock_get_ms(void *user)
{
    return ((mock_hw_t *)user)->now_ms;
}

static const sc8701_hal_t k_hal = {
    mock_ce_set,
    mock_itune_set,
    mock_pg_get,
    mock_pwm_vout_init,
    mock_pwm_vout_set,
    mock_pwm_ilim_init,
    mock_pwm_ilim_set,
    mock_adc_read_mv,
    mock_delay_ms,
    mock_get_ms,
};

static sc8701_hw_config_t default_config(void)
{
    sc8701_hw_config_t cfg;

    cfg.r_up_ohm = 100000U;
    cfg.r_down_ohm = 11000U;
    cfg.rilim1_ohm = 240U;
    cfg.rilim2_ohm = 400U;
    cfg.rsns1_uohm = 10000U;
    cfg.rsns2_uohm = 10000U;
    cfg.pwm_vout_freq_hz = 50000U;
    cfg.pwm_ilim_freq_hz = 50000U;
    return cfg;
}

static int test_init_validation(void)
{
    sc8701_ctx_t ctx;
    mock_hw_t mock;
    sc8701_hw_config_t cfg = default_config();
    sc8701_hal_t bad_hal = k_hal;

    memset(&ctx, 0, sizeof(ctx));
    memset(&mock, 0, sizeof(mock));

    ASSERT_EQ_INT(SC8701_ERR_NULL, sc8701_init(NULL, &k_hal, &mock, &cfg));
    ASSERT_EQ_INT(SC8701_ERR_NULL, sc8701_init(&ctx, NULL, &mock, &cfg));
    ASSERT_EQ_INT(SC8701_ERR_NULL, sc8701_init(&ctx, &k_hal, &mock, NULL));

    bad_hal.ce_set = NULL;
    ASSERT_EQ_INT(SC8701_ERR_HW, sc8701_init(&ctx, &bad_hal, &mock, &cfg));

    bad_hal = k_hal;
    bad_hal.adc_read_mv = NULL;
    ASSERT_EQ_INT(SC8701_ERR_HW, sc8701_init(&ctx, &bad_hal, &mock, &cfg));

    cfg = default_config();
    cfg.r_up_ohm = 0U;
    ASSERT_EQ_INT(SC8701_ERR_PARAM, sc8701_init(&ctx, &k_hal, &mock, &cfg));

    cfg = default_config();
    cfg.r_down_ohm = 0U;
    ASSERT_EQ_INT(SC8701_ERR_PARAM, sc8701_init(&ctx, &k_hal, &mock, &cfg));

    cfg = default_config();
    cfg.pwm_vout_freq_hz = SC8701_PWM_FREQ_MIN_HZ - 1U;
    ASSERT_EQ_INT(SC8701_ERR_RANGE, sc8701_init(&ctx, &k_hal, &mock, &cfg));

    cfg = default_config();
    mock.fail_vout_init = -1;
    ASSERT_EQ_INT(SC8701_ERR_HW, sc8701_init(&ctx, &k_hal, &mock, &cfg));

    memset(&mock, 0, sizeof(mock));
    ASSERT_EQ_INT(SC8701_OK, sc8701_init(&ctx, &k_hal, &mock, &cfg));
    ASSERT_EQ_U32(50000U, mock.vout_freq_hz);
    ASSERT_EQ_U32(50000U, mock.ilim_freq_hz);
    ASSERT_EQ_INT(1, mock.ce_level);
    ASSERT_EQ_INT(SC8701_ITUNE_INPUT, mock.itune_level);
    ASSERT_NEAR_FLOAT(1.0f, mock.vout_duty, 0.0001f);
    ASSERT_NEAR_FLOAT(1.0f, mock.ilim_duty, 0.0001f);

    return 0;
}

static int test_vout_and_ilim_calculation(void)
{
    sc8701_ctx_t ctx;
    mock_hw_t mock;
    sc8701_hw_config_t cfg = default_config();
    uint32_t vout_set;
    uint32_t vout_min;

    memset(&ctx, 0, sizeof(ctx));
    memset(&mock, 0, sizeof(mock));
    ASSERT_EQ_INT(SC8701_OK, sc8701_init(&ctx, &k_hal, &mock, &cfg));

    vout_set = sc8701_get_vout_set_mv(&ctx);
    vout_min = vout_set / 6U;
    ASSERT_EQ_U32(12310U, vout_set);
    ASSERT_EQ_U32(5041U, sc8701_get_iin_limit_ma(&ctx));
    ASSERT_EQ_U32(3025U, sc8701_get_iout_limit_ma(&ctx));

    ASSERT_EQ_INT(SC8701_OK, sc8701_set_vout(&ctx, vout_min));
    ASSERT_NEAR_FLOAT(0.0f, mock.vout_duty, 0.0001f);

    ASSERT_EQ_INT(SC8701_OK, sc8701_set_vout(&ctx, vout_set / 2U));
    ASSERT_NEAR_FLOAT(0.3999f, mock.vout_duty, 0.001f);

    ASSERT_EQ_INT(SC8701_OK, sc8701_set_vout(&ctx, vout_set));
    ASSERT_NEAR_FLOAT(1.0f, mock.vout_duty, 0.0001f);

    ASSERT_EQ_INT(SC8701_ERR_RANGE, sc8701_set_vout(&ctx, vout_min - 1U));
    ASSERT_EQ_INT(SC8701_ERR_RANGE, sc8701_set_vout(&ctx, vout_set + 1U));

    ASSERT_EQ_INT(SC8701_OK, sc8701_set_ilim(&ctx, 2520U));
    ASSERT_NEAR_FLOAT(0.4999f, mock.ilim_duty, 0.001f);
    ASSERT_EQ_INT(SC8701_ERR_RANGE, sc8701_set_ilim(&ctx, 0U));
    ASSERT_EQ_INT(SC8701_ERR_RANGE,
                  sc8701_set_ilim(&ctx, sc8701_get_iin_limit_ma(&ctx) + 1U));

    ASSERT_EQ_INT(SC8701_OK,
                  sc8701_set_itune_target(&ctx, SC8701_ITUNE_OUTPUT));
    ASSERT_EQ_INT(SC8701_ITUNE_OUTPUT, mock.itune_level);
    ASSERT_EQ_INT(SC8701_OK,
                  sc8701_set_ilim(&ctx, sc8701_get_iout_limit_ma(&ctx)));
    ASSERT_NEAR_FLOAT(1.0f, mock.ilim_duty, 0.0001f);
    ASSERT_EQ_INT(SC8701_ERR_PARAM,
                  sc8701_set_itune_target(&ctx, (sc8701_itune_target_t)2));

    return 0;
}

static int test_pg_success_and_timeout(void)
{
    sc8701_ctx_t ctx;
    mock_hw_t mock;
    sc8701_hw_config_t cfg = default_config();

    memset(&ctx, 0, sizeof(ctx));
    memset(&mock, 0, sizeof(mock));
    mock.pg_level = 1U;
    ASSERT_EQ_INT(SC8701_OK, sc8701_init(&ctx, &k_hal, &mock, &cfg));
    ASSERT_EQ_INT(SC8701_OK, sc8701_enable(&ctx, 5U));
    ASSERT_EQ_INT(0, mock.ce_level);
    ASSERT_EQ_INT(1, sc8701_is_enabled(&ctx));
    ASSERT_EQ_INT(1, sc8701_get_pg(&ctx));
    ASSERT_EQ_U32(SC8701_SOFT_START_MS_MAX, mock.delay_total_ms);

    ASSERT_EQ_INT(SC8701_OK, sc8701_disable(&ctx));
    ASSERT_EQ_INT(1, mock.ce_level);
    ASSERT_EQ_INT(0, sc8701_is_enabled(&ctx));
    ASSERT_EQ_INT(0, sc8701_get_fault(&ctx));

    memset(&ctx, 0, sizeof(ctx));
    memset(&mock, 0, sizeof(mock));
    mock.pg_level = 0U;
    ASSERT_EQ_INT(SC8701_OK, sc8701_init(&ctx, &k_hal, &mock, &cfg));
    ASSERT_EQ_INT(SC8701_ERR_PG_TIMEOUT, sc8701_enable(&ctx, 3U));
    ASSERT_EQ_INT(1, mock.ce_level);
    ASSERT_EQ_INT(0, sc8701_is_enabled(&ctx));
    ASSERT_EQ_INT(0, sc8701_get_pg(&ctx));
    ASSERT_EQ_INT(1, sc8701_get_fault(&ctx));

    ASSERT_EQ_INT(SC8701_ERR_FAULT, sc8701_enable(&ctx, 1U));
    ASSERT_EQ_INT(SC8701_OK, sc8701_clear_fault(&ctx));
    mock.pg_level = 1U;
    ASSERT_EQ_INT(SC8701_OK, sc8701_enable(&ctx, 1U));

    return 0;
}

static int test_multi_instance_and_mode(void)
{
    sc8701_ctx_t ctx_a;
    sc8701_ctx_t ctx_b;
    mock_hw_t mock_a;
    mock_hw_t mock_b;
    sc8701_hw_config_t cfg_a = default_config();
    sc8701_hw_config_t cfg_b = default_config();

    memset(&ctx_a, 0, sizeof(ctx_a));
    memset(&ctx_b, 0, sizeof(ctx_b));
    memset(&mock_a, 0, sizeof(mock_a));
    memset(&mock_b, 0, sizeof(mock_b));

    cfg_b.r_up_ohm = 200000U;
    cfg_b.r_down_ohm = 20000U;
    ASSERT_EQ_INT(SC8701_OK, sc8701_init(&ctx_a, &k_hal, &mock_a, &cfg_a));
    ASSERT_EQ_INT(SC8701_OK, sc8701_init(&ctx_b, &k_hal, &mock_b, &cfg_b));

    ASSERT_EQ_INT(SC8701_OK, sc8701_set_vout(&ctx_a,
                                             sc8701_get_vout_set_mv(&ctx_a) / 6U));
    ASSERT_EQ_INT(SC8701_OK, sc8701_set_vout(&ctx_b,
                                             sc8701_get_vout_set_mv(&ctx_b)));
    ASSERT_NEAR_FLOAT(0.0f, mock_a.vout_duty, 0.0001f);
    ASSERT_NEAR_FLOAT(1.0f, mock_b.vout_duty, 0.0001f);

    ctx_a.enabled = 1U;
    mock_a.adc[SC8701_ADC_VIN] = 12000U;
    mock_a.adc[SC8701_ADC_VOUT] = 5000U;
    ASSERT_EQ_INT(SC8701_MODE_BUCK, sc8701_get_mode(&ctx_a));

    mock_a.adc[SC8701_ADC_VIN] = 5000U;
    mock_a.adc[SC8701_ADC_VOUT] = 12000U;
    ASSERT_EQ_INT(SC8701_MODE_BOOST, sc8701_get_mode(&ctx_a));

    mock_a.adc[SC8701_ADC_VIN] = 5000U;
    mock_a.adc[SC8701_ADC_VOUT] = 5300U;
    ASSERT_EQ_INT(SC8701_MODE_BUCK_BOOST, sc8701_get_mode(&ctx_a));

    ctx_a.fault = 1U;
    ASSERT_EQ_INT(SC8701_MODE_FAULT, sc8701_get_mode(&ctx_a));
    ctx_a.fault = 0U;
    ctx_a.enabled = 0U;
    ASSERT_EQ_INT(SC8701_MODE_OFF, sc8701_get_mode(&ctx_a));

    return 0;
}

int main(void)
{
    ASSERT_EQ_INT(0, test_init_validation());
    ASSERT_EQ_INT(0, test_vout_and_ilim_calculation());
    ASSERT_EQ_INT(0, test_pg_success_and_timeout());
    ASSERT_EQ_INT(0, test_multi_instance_and_mode());

    printf("SC8701 tests passed\n");
    return 0;
}
