#include "pd_app.h"

static usbpd_config_t g_config;

static void app_log(const char *text, void *arg)
{
    (void)text;
    (void)arg;
    /* Bind this callback to the product's non-blocking console implementation. */
}

void usbpd_app_init(void)
{
    g_config.port_type = USBPD_PORT_SINK;
    g_config.sink.max_voltage_mv = 5000U;
    g_config.sink.max_current_ma = 1000U;
    g_config.sink.max_power_mw = 5000U;
    g_config.sink.sink_pdo_count = 1U;
    g_config.sink.sink_pdo[0].sink_fixed.current_10ma = 100U;
    g_config.sink.sink_pdo[0].sink_fixed.voltage_50mv = 100U;
    g_config.sink.sink_pdo[0].sink_fixed.type = USBPD_PDO_FIXED;
    (void)usbpd_init(0U, &g_config);
}

void usbpd_app_task(uint32_t now_ms)
{
    usbpd_task(0U, now_ms);
    usbpd_observer_service(1U, app_log, 0);
}
