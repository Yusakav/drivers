#include "message_buffer.h"
#include "usbpd_message.h"
#include <assert.h>
#include <string.h>

static char g_line[160];
static void capture(const char *text, void *arg)
{
    (void)arg;
    strncpy(g_line, text, sizeof(g_line) - 1U);
    g_line[sizeof(g_line) - 1U] = '\0';
}

int main(void)
{
    struct message_buffer_t buffer;
    struct message_buffer_record_t in = {0};
    struct message_buffer_record_t out;
    uint16_t i;

    message_buffer_init(&buffer);
    in.length = 2U;
    in.payload[0] = 0x01U;
    in.payload[1] = 0x00U;
    for (i = 0U; i < USBPD_MESSAGE_BUFFER_DEPTH + 1U; ++i) {
        in.timestamp_ms = i;
        assert(message_buffer_push(&buffer, &in) != 0U);
    }
    assert(message_buffer_count(&buffer) == USBPD_MESSAGE_BUFFER_DEPTH);
    assert(message_buffer_dropped(&buffer) == 1U);
    assert(message_buffer_pop(&buffer, &out) != 0U);
    assert(out.timestamp_ms == 1U);

    usbpd_observer_init();
    usbpd_observer_record(MESSAGE_BUFFER_RX, USBPD_SOP, in.payload, in.length, 42U, 0U);
    usbpd_observer_service(1U, capture, 0);
    assert(strstr(g_line, "RX") != 0);
    assert(strstr(g_line, "GOODCRC") != 0);
    return 0;
}
