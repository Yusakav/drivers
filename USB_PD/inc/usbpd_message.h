#ifndef USBPD_MESSAGE_H
#define USBPD_MESSAGE_H

#include "usbpd_def.h"
#include "message_buffer.h"

enum usbpd_observer_flag_e
{
    USBPD_OBSERVER_GOODCRC = 1U,
    USBPD_OBSERVER_ERROR = 2U,
    USBPD_OBSERVER_RESET = 4U,
};

typedef void (*usbpd_observer_write_fn)(const char *text, void *arg);

void usbpd_observer_record(uint8_t direction, uint8_t sop, const uint8_t *raw, uint8_t length, uint32_t timestamp_ms,
                           uint8_t flags);
uint8_t usbpd_observer_pop(struct message_buffer_record_t *record);
void usbpd_observer_clear(void);
uint16_t usbpd_observer_count(void);
uint32_t usbpd_observer_dropped(void);
void usbpd_observer_service(uint8_t max_records, usbpd_observer_write_fn write, void *arg);

void usbpd_observer_init(void);

#endif
