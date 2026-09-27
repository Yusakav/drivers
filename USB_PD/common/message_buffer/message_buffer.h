#ifndef USBPD_MESSAGE_BUFFER_H
#define USBPD_MESSAGE_BUFFER_H

#include <stdint.h>

#ifndef USBPD_MESSAGE_BUFFER_DEPTH
#define USBPD_MESSAGE_BUFFER_DEPTH 32U
#endif

#ifndef USBPD_MESSAGE_BUFFER_PAYLOAD_MAX
#define USBPD_MESSAGE_BUFFER_PAYLOAD_MAX 34U
#endif

enum message_buffer_direction_e { MESSAGE_BUFFER_RX = 0U, MESSAGE_BUFFER_TX = 1U };
typedef void (*message_buffer_lock_fn)(void *arg);

struct message_buffer_record_t {
    uint32_t timestamp_ms;
    uint8_t direction;
    uint8_t channel;
    uint8_t flags;
    uint8_t length;
    uint8_t payload[USBPD_MESSAGE_BUFFER_PAYLOAD_MAX];
};

struct message_buffer_t {
    struct message_buffer_record_t records[USBPD_MESSAGE_BUFFER_DEPTH];
    uint16_t read_index;
    uint16_t write_index;
    uint16_t count;
    uint32_t dropped;
    message_buffer_lock_fn lock;
    message_buffer_lock_fn unlock;
    void *lock_arg;
};

void message_buffer_init(struct message_buffer_t *buffer);
void message_buffer_set_lock(struct message_buffer_t *buffer, message_buffer_lock_fn lock,
                             message_buffer_lock_fn unlock, void *arg);
uint8_t message_buffer_push(struct message_buffer_t *buffer,
                            const struct message_buffer_record_t *record);
uint8_t message_buffer_pop(struct message_buffer_t *buffer,
                           struct message_buffer_record_t *record);
void message_buffer_clear(struct message_buffer_t *buffer);
uint16_t message_buffer_count(const struct message_buffer_t *buffer);
uint32_t message_buffer_dropped(const struct message_buffer_t *buffer);

#endif
