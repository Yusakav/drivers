#include "ld2410d.h"
#include "ld2410d_protocol.h"
#include "../example/ld2410d_app.c"
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t tx[512];
    uint16_t tx_len;
    uint8_t rx[2048];
    uint16_t rx_len;
    uint16_t rx_pos;
    uint16_t chunk;
    uint16_t flush_count;
    uint32_t now_ms;
    uint8_t callback_count;
    ld2410d_detect_status_t callback_status;
    uint16_t callback_distance;
} mock_uart_t;

static uint8_t g_failures;

static void expect_true(int cond, const char *name)
{
    if (!cond) {
        printf("FAIL: %s\n", name);
        g_failures++;
    }
}

static void mock_send(void *user, const uint8_t *data, uint16_t len)
{
    mock_uart_t *mock = (mock_uart_t *)user;
    expect_true((size_t)mock->tx_len + len <= sizeof(mock->tx), "tx overflow");
    memcpy(mock->tx + mock->tx_len, data, len);
    mock->tx_len = (uint16_t)(mock->tx_len + len);
}

static uint16_t mock_recv(void *user, uint8_t *buf, uint16_t max_len,
                          uint32_t timeout_ms)
{
    mock_uart_t *mock = (mock_uart_t *)user;
    uint16_t available;
    uint16_t n;

    (void)timeout_ms;
    if (mock->rx_pos >= mock->rx_len) {
        return 0U;
    }

    available = (uint16_t)(mock->rx_len - mock->rx_pos);
    n = available < max_len ? available : max_len;
    if (mock->chunk != 0U && n > mock->chunk) {
        n = mock->chunk;
    }

    memcpy(buf, mock->rx + mock->rx_pos, n);
    mock->rx_pos = (uint16_t)(mock->rx_pos + n);
    return n;
}

static void mock_flush(void *user)
{
    mock_uart_t *mock = (mock_uart_t *)user;
    mock->flush_count++;
}

static uint32_t mock_get_ms(void *user)
{
    mock_uart_t *mock = (mock_uart_t *)user;
    mock->now_ms += 100U;
    return mock->now_ms;
}

static void mock_target_cb(void *user, ld2410d_detect_status_t status,
                           uint16_t distance)
{
    mock_uart_t *mock = (mock_uart_t *)user;
    mock->callback_count++;
    mock->callback_status = status;
    mock->callback_distance = distance;
}

static void mock_reset(mock_uart_t *mock)
{
    memset(mock, 0, sizeof(*mock));
}

static ld2410d_uart_ops_t mock_ops(mock_uart_t *mock)
{
    ld2410d_uart_ops_t ops;

    ops.send = mock_send;
    ops.recv = mock_recv;
    ops.flush = mock_flush;
    ops.user = mock;
    return ops;
}

static void append_ack(mock_uart_t *mock, uint16_t cmd, uint16_t status,
                       const uint8_t *payload, uint16_t payload_len)
{
    uint8_t frame[128];
    uint16_t len = (uint16_t)(4U + payload_len);
    uint16_t total = (uint16_t)(6U + len + 4U);
    uint8_t *p = frame;

    expect_true(total <= sizeof(frame), "ack frame local overflow");
    *p++ = LD2410D_FRAME_HEADER_0;
    *p++ = LD2410D_FRAME_HEADER_1;
    *p++ = LD2410D_FRAME_HEADER_2;
    *p++ = LD2410D_FRAME_HEADER_3;
    ld2410d_put_u16_le(p, len);
    p += 2;
    ld2410d_put_u16_le(p, cmd);
    p += 2;
    ld2410d_put_u16_le(p, status);
    p += 2;
    if (payload_len != 0U) {
        memcpy(p, payload, payload_len);
        p += payload_len;
    }
    *p++ = LD2410D_FRAME_FOOTER_0;
    *p++ = LD2410D_FRAME_FOOTER_1;
    *p++ = LD2410D_FRAME_FOOTER_2;
    *p++ = LD2410D_FRAME_FOOTER_3;

    expect_true((size_t)mock->rx_len + total <= sizeof(mock->rx),
                "rx append overflow");
    memcpy(mock->rx + mock->rx_len, frame, total);
    mock->rx_len = (uint16_t)(mock->rx_len + total);
}

static void append_string_ack(mock_uart_t *mock, uint16_t cmd, const char *text)
{
    uint8_t payload[64];
    uint16_t text_len = (uint16_t)strlen(text);

    ld2410d_put_u16_le(payload, text_len);
    memcpy(payload + 2U, text, text_len);
    append_ack(mock, cmd, 0U, payload, (uint16_t)(text_len + 2U));
}

static void append_param_ack(mock_uart_t *mock, uint32_t value)
{
    uint8_t payload[4];

    ld2410d_put_u32_le(payload, value);
    append_ack(mock, LD2410D_CMD_READ_PARAM, 0U, payload, sizeof(payload));
}

static uint16_t build_engineering_frame(uint8_t *buf,
                                        ld2410d_detect_status_t status,
                                        uint16_t distance)
{
    uint8_t *p = buf;
    uint8_t i;

    *p++ = LD2410D_DATA_HEADER_0;
    *p++ = LD2410D_DATA_HEADER_1;
    *p++ = LD2410D_DATA_HEADER_2;
    *p++ = LD2410D_DATA_HEADER_3;
    ld2410d_put_u16_le(p, 131U);
    p += 2;
    *p++ = (uint8_t)status;
    ld2410d_put_u16_le(p, distance);
    p += 2;
    for (i = 0U; i < LD2410D_GATE_COUNT; i++) {
        ld2410d_put_u32_le(p, (uint32_t)(i + 1U));
        p += 4;
    }
    for (i = 0U; i < LD2410D_GATE_COUNT; i++) {
        ld2410d_put_u32_le(p, (uint32_t)(i + 101U));
        p += 4;
    }
    *p++ = LD2410D_DATA_FOOTER_0;
    *p++ = LD2410D_DATA_FOOTER_1;
    *p++ = LD2410D_DATA_FOOTER_2;
    *p++ = LD2410D_DATA_FOOTER_3;
    return (uint16_t)(p - buf);
}

static void test_build_command_frame(void)
{
    uint8_t payload[2];
    uint8_t frame[32];
    uint16_t frame_len = 0U;
    const uint8_t expected[] = {
        0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF,
        0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01
    };

    ld2410d_put_u16_le(payload, 1U);
    expect_true(ld2410d_build_command_frame(frame, sizeof(frame),
                                            LD2410D_CMD_ENABLE_CFG,
                                            payload, sizeof(payload),
                                            &frame_len) == LD2410D_OK,
                "build enable frame returns ok");
    expect_true(frame_len == sizeof(expected), "build enable frame len");
    expect_true(memcmp(frame, expected, sizeof(expected)) == 0,
                "build enable frame bytes");
}

static void test_driver_commands_and_split_ack(void)
{
    mock_uart_t mock;
    ld2410d_t dev;
    ld2410d_uart_ops_t ops;
    uint32_t value = 0U;
    const uint8_t expected_enable[] = {
        0xFD, 0xFC, 0xFB, 0xFA, 0x04, 0x00, 0xFF,
        0x00, 0x01, 0x00, 0x04, 0x03, 0x02, 0x01
    };

    mock_reset(&mock);
    mock.chunk = 3U;
    ops = mock_ops(&mock);
    append_ack(&mock, LD2410D_CMD_ENABLE_CFG, 0U, NULL, 0U);

    expect_true(ld2410d_init(&dev, &ops) == LD2410D_OK, "driver init ok");
    expect_true(ld2410d_enable_config(&dev) == LD2410D_OK,
                "enable config split ack");
    expect_true(mock.tx_len == sizeof(expected_enable), "enable tx len");
    expect_true(memcmp(mock.tx, expected_enable, sizeof(expected_enable)) == 0,
                "enable tx bytes");

    mock_reset(&mock);
    mock.chunk = 2U;
    ops = mock_ops(&mock);
    expect_true(ld2410d_init(&dev, &ops) == LD2410D_OK, "driver reinit ok");
    append_param_ack(&mock, 0x12345678UL);
    expect_true(ld2410d_read_param(&dev, LD2410D_PARAM_MAX_DISTANCE, &value) ==
                    LD2410D_OK,
                "read param split ack");
    expect_true(value == 0x12345678UL, "read param value");

    mock_reset(&mock);
    ops = mock_ops(&mock);
    expect_true(ld2410d_init(&dev, &ops) == LD2410D_OK, "driver mode init ok");
    append_ack(&mock, LD2410D_CMD_SET_OUTPUT_MODE, 0U, NULL, 0U);
    expect_true(ld2410d_set_output_mode(&dev, LD2410D_OUTPUT_MODE_ENGINEERING) ==
                    LD2410D_OK,
                "set output mode ok");
    expect_true(dev.output_mode == LD2410D_OUTPUT_MODE_ENGINEERING,
                "output mode cached");
}

static void test_bad_ack_frames(void)
{
    uint8_t frame[14];
    ld2410d_ack_frame_t ack;

    frame[0] = LD2410D_FRAME_HEADER_0;
    frame[1] = LD2410D_FRAME_HEADER_1;
    frame[2] = LD2410D_FRAME_HEADER_2;
    frame[3] = LD2410D_FRAME_HEADER_3;
    ld2410d_put_u16_le(&frame[4], 4U);
    ld2410d_put_u16_le(&frame[6], LD2410D_CMD_END_CFG);
    ld2410d_put_u16_le(&frame[8], 0U);
    frame[10] = LD2410D_FRAME_FOOTER_0;
    frame[11] = LD2410D_FRAME_FOOTER_1;
    frame[12] = LD2410D_FRAME_FOOTER_2;
    frame[13] = LD2410D_FRAME_FOOTER_3;

    frame[0] = 0U;
    expect_true(ld2410d_parse_ack_frame(frame, 14U, LD2410D_CMD_END_CFG, &ack) ==
                    LD2410D_ERR_FRAME,
                "bad ack header");
    frame[0] = LD2410D_FRAME_HEADER_0;

    frame[13] = 0U;
    expect_true(ld2410d_parse_ack_frame(frame, 14U, LD2410D_CMD_END_CFG, &ack) ==
                    LD2410D_ERR_FRAME,
                "bad ack footer");
    frame[13] = LD2410D_FRAME_FOOTER_3;

    ld2410d_put_u16_le(&frame[6], LD2410D_CMD_READ_PARAM);
    expect_true(ld2410d_parse_ack_frame(frame, 14U, LD2410D_CMD_END_CFG, &ack) ==
                    LD2410D_ERR_FRAME,
                "bad ack command");
    ld2410d_put_u16_le(&frame[6], LD2410D_CMD_END_CFG);

    ld2410d_put_u16_le(&frame[8], 1U);
    expect_true(ld2410d_parse_ack_frame(frame, 14U, LD2410D_CMD_END_CFG, &ack) ==
                    LD2410D_ERR_ACK,
                "nonzero ack status");

    ld2410d_put_u16_le(&frame[8], 0U);
    ld2410d_put_u16_le(&frame[4], 0xFFFFU);
    expect_true(ld2410d_parse_ack_frame(frame, 14U, LD2410D_CMD_END_CFG, &ack) ==
                    LD2410D_ERR_FRAME,
                "oversized ack length does not wrap");
}

static void test_ack_overflow_during_receive(void)
{
    mock_uart_t mock;
    ld2410d_t dev;
    ld2410d_uart_ops_t ops;
    uint32_t value = 0U;

    mock_reset(&mock);
    ops = mock_ops(&mock);
    mock.rx[0] = LD2410D_FRAME_HEADER_0;
    mock.rx[1] = LD2410D_FRAME_HEADER_1;
    mock.rx[2] = LD2410D_FRAME_HEADER_2;
    mock.rx[3] = LD2410D_FRAME_HEADER_3;
    ld2410d_put_u16_le(&mock.rx[4], 300U);
    mock.rx_len = 6U;

    expect_true(ld2410d_init(&dev, &ops) == LD2410D_OK, "overflow init ok");
    expect_true(ld2410d_read_param(&dev, LD2410D_PARAM_MAX_DISTANCE, &value) ==
                    LD2410D_ERR_OVERFLOW,
                "ack len overflow");
}

static void test_engineering_frame_parse(void)
{
    uint8_t frame[160];
    uint16_t frame_len;
    ld2410d_engineering_data_t data;

    frame_len = build_engineering_frame(frame, LD2410D_DETECT_STATIC, 234U);
    expect_true(ld2410d_parse_engineering_frame(frame, frame_len, &data) ==
                    LD2410D_OK,
                "engineering parse ok");
    expect_true(data.status == LD2410D_DETECT_STATIC, "engineering status");
    expect_true(data.distance == 234U, "engineering distance");
    expect_true(data.gates[0].motion_energy == 1U, "engineering motion first");
    expect_true(data.gates[15].motion_energy == 16U, "engineering motion last");
    expect_true(data.gates[0].static_energy == 101U, "engineering static first");
    expect_true(data.gates[15].static_energy == 116U, "engineering static last");

    frame[0] = 0U;
    expect_true(ld2410d_parse_engineering_frame(frame, frame_len, &data) ==
                    LD2410D_ERR_FRAME,
                "engineering bad header");
}

static void test_recv_engineering_frame(void)
{
    mock_uart_t mock;
    ld2410d_t dev;
    ld2410d_uart_ops_t ops;
    ld2410d_engineering_data_t data;
    uint16_t frame_len;

    mock_reset(&mock);
    mock.chunk = 7U;
    ops = mock_ops(&mock);
    mock.rx[0] = 0x55U;
    mock.rx_len = 1U;
    frame_len = build_engineering_frame(mock.rx + mock.rx_len,
                                        LD2410D_DETECT_MOVING, 88U);
    mock.rx_len = (uint16_t)(mock.rx_len + frame_len);

    expect_true(ld2410d_init(&dev, &ops) == LD2410D_OK, "recv eng init");
    expect_true(ld2410d_recv_engineering_frame(&dev, &data, 200U) ==
                    LD2410D_OK,
                "recv engineering split frame");
    expect_true(mock.flush_count == 1U, "recv engineering flush");
    expect_true(data.status == LD2410D_DETECT_MOVING, "recv engineering status");
    expect_true(data.distance == 88U, "recv engineering distance");
}

static void test_app_init_and_progression(void)
{
    mock_uart_t mock;
    ld2410d_app_t app;
    ld2410d_app_config_t cfg;
    uint8_t frame[160];
    uint16_t frame_len;

    memset(&app, 0, sizeof(app));
    memset(&cfg, 0, sizeof(cfg));
    expect_true(ld2410d_app_init(&app, &cfg) == LD2410D_ERR_NULL,
                "app init missing callbacks");

    mock_reset(&mock);
    memset(&cfg, 0, sizeof(cfg));
    cfg.uart = mock_ops(&mock);
    cfg.get_ms = mock_get_ms;
    cfg.time_user = &mock;
    cfg.target_cb = mock_target_cb;
    cfg.target_user = &mock;
    cfg.max_distance = 85U;
    cfg.disappear_delay = 30U;
    cfg.output_mode = LD2410D_OUTPUT_MODE_ENGINEERING;
    cfg.save_params = 1U;
    cfg.receive_timeout_ms = 200U;
    cfg.report_interval_ms = 10000U;

    append_ack(&mock, LD2410D_CMD_ENABLE_CFG, 0U, NULL, 0U);
    append_string_ack(&mock, LD2410D_CMD_READ_VERSION, "1.2.3");
    append_string_ack(&mock, LD2410D_CMD_READ_SN_CHAR, "SN1234");
    append_ack(&mock, LD2410D_CMD_END_CFG, 0U, NULL, 0U);
    append_ack(&mock, LD2410D_CMD_ENABLE_CFG, 0U, NULL, 0U);
    append_ack(&mock, LD2410D_CMD_WRITE_PARAM, 0U, NULL, 0U);
    append_ack(&mock, LD2410D_CMD_WRITE_PARAM, 0U, NULL, 0U);
    append_ack(&mock, LD2410D_CMD_SET_OUTPUT_MODE, 0U, NULL, 0U);
    append_ack(&mock, LD2410D_CMD_SAVE_PARAM, 0U, NULL, 0U);
    append_ack(&mock, LD2410D_CMD_END_CFG, 0U, NULL, 0U);
    frame_len = build_engineering_frame(frame, LD2410D_DETECT_STATIC, 321U);
    memcpy(mock.rx + mock.rx_len, frame, frame_len);
    mock.rx_len = (uint16_t)(mock.rx_len + frame_len);

    expect_true(ld2410d_app_init(&app, &cfg) == LD2410D_OK, "app init valid");
    expect_true(ld2410d_app_get_state(&app) == LD2410D_APP_STATE_INIT,
                "app initial state");

    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app process init");
    expect_true(ld2410d_app_get_state(&app) == LD2410D_APP_STATE_CHECK,
                "app enters check");
    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app check enable");
    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app check version");
    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app check sn");
    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app check end");
    expect_true(ld2410d_app_get_state(&app) == LD2410D_APP_STATE_CONFIGURE,
                "app enters configure");
    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app cfg enable");
    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app cfg basic");
    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app cfg output");
    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app cfg save");
    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app cfg end");
    expect_true(ld2410d_app_get_state(&app) == LD2410D_APP_STATE_OPERATION,
                "app enters operation");
    expect_true(ld2410d_app_process(&app) == LD2410D_OK, "app operation frame");
    expect_true(mock.callback_count == 1U, "app target callback count");
    expect_true(mock.callback_status == LD2410D_DETECT_STATIC,
                "app target callback status");
    expect_true(mock.callback_distance == 321U, "app target callback distance");
}

int main(void)
{
    test_build_command_frame();
    test_driver_commands_and_split_ack();
    test_bad_ack_frames();
    test_ack_overflow_during_receive();
    test_engineering_frame_parse();
    test_recv_engineering_frame();
    test_app_init_and_progression();

    if (g_failures != 0U) {
        printf("%u test failure(s)\n", (unsigned)g_failures);
        return 1;
    }

    printf("All LD2410D tests passed\n");
    return 0;
}
