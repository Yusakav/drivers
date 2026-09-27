#include "usbpd_types.h"
#include <assert.h>

int main(void)
{
    union usbpd_header_u header;
    union usbpd_pdo_u pdo;
    union usbpd_epr_mode_do_u epr;
    union usbpd_bist_do_u bist;
    union usbpd_alert_do_u alert;

    header.raw = 0U;
    header.bits.type = USBPD_DATA_SOURCE_CAP;
    header.bits.revision = USBPD_REV30;
    header.bits.message_id = 3U;
    header.bits.data_objects = 2U;
    assert(header.raw == 0x2681U);

    pdo.raw = 0U;
    pdo.fixed.current_10ma = 300U;
    pdo.fixed.voltage_50mv = 180U;
    pdo.fixed.type = USBPD_PDO_FIXED;
    assert((pdo.raw & 0x3FFU) == 300U);
    assert(((pdo.raw >> 10) & 0x3FFU) == 180U);

    assert(USBPD_CTRL_DATA_RESET == 14U);
    assert(USBPD_CTRL_GET_REVISION == 24U);
    assert(USBPD_DATA_BIST == 3U);
    assert(USBPD_DATA_REVISION == 12U);
    assert(USBPD_EXT_EPR_SOURCE_CAP == 17U);
    assert(USBPD_EXT_VENDOR_DEFINED == 30U);
    assert(USBPD_EXT_CTRL_EPR_KEEPALIVE_ACK == 4U);
    assert(sizeof(union usbpd_ext_header_u) == 2U);
    assert(sizeof(union usbpd_rdo_u) == 4U);
    assert(sizeof(union usbpd_vdm_header_u) == 4U);
    assert(sizeof(struct usbpd_sink_cap_ext_db_t) == 24U);
    assert(sizeof(struct usbpd_source_cap_ext_db_t) == 25U);
    assert(sizeof(struct usbpd_status_db_t) == 7U);
    assert(sizeof(struct usbpd_battery_cap_db_t) == 9U);
    assert(sizeof(struct usbpd_pps_status_db_t) == 4U);
    assert(sizeof(struct usbpd_manufacturer_info_db_t) == 26U);
    assert(sizeof(struct usbpd_country_codes_db_t) == 26U);
    assert(sizeof(struct usbpd_country_info_db_t) == 26U);

    pdo.raw = 0U;
    pdo.variable.current_10ma = 300U;
    pdo.variable.min_voltage_50mv = 100U;
    pdo.variable.max_voltage_50mv = 400U;
    pdo.variable.type = USBPD_PDO_VARIABLE;
    assert((pdo.raw >> 30) == USBPD_PDO_VARIABLE);

    epr.raw = 0U;
    epr.bits.action = USBPD_EPR_MODE_ENTER;
    epr.bits.data = 140U;
    assert(epr.raw == 0x018C0000UL);

    bist.raw = 0U;
    bist.bits.mode = USBPD_BIST_TEST_DATA;
    assert(bist.raw == 0x80000000UL);

    alert.raw = 0U;
    alert.bits.extended_event_type = 4U;
    alert.bits.hot_swappable_batteries = 1U;
    alert.bits.fixed_batteries = 2U;
    alert.bits.type = USBPD_ALERT_EXTENDED;
    assert(alert.raw == 0x80210004UL);
    return 0;
}
