#include "message_buffer.h"
#include <string.h>

static void buffer_lock(const struct message_buffer_t *buffer)
{
    if (buffer->lock != 0) buffer->lock(buffer->lock_arg);
}

static void buffer_unlock(const struct message_buffer_t *buffer)
{
    if (buffer->unlock != 0) buffer->unlock(buffer->lock_arg);
}

void message_buffer_init(struct message_buffer_t *buffer)
{
    if (buffer != 0) memset(buffer, 0, sizeof(*buffer));
}

void message_buffer_set_lock(struct message_buffer_t *buffer, message_buffer_lock_fn lock,
                             message_buffer_lock_fn unlock, void *arg)
{
    if (buffer == 0) return;
    buffer->lock = ((lock != 0) && (unlock != 0)) ? lock : 0;
    buffer->unlock = ((lock != 0) && (unlock != 0)) ? unlock : 0;
    buffer->lock_arg = ((lock != 0) && (unlock != 0)) ? arg : 0;
}

uint8_t message_buffer_push(struct message_buffer_t *buffer,
                            const struct message_buffer_record_t *record)
{
    if ((buffer == 0) || (record == 0)) return 0U;
    buffer_lock(buffer);
    if (buffer->count == USBPD_MESSAGE_BUFFER_DEPTH) {
        buffer->read_index = (uint16_t)((buffer->read_index + 1U) % USBPD_MESSAGE_BUFFER_DEPTH);
        buffer->count--;
        buffer->dropped++;
    }
    buffer->records[buffer->write_index] = *record;
    buffer->write_index = (uint16_t)((buffer->write_index + 1U) % USBPD_MESSAGE_BUFFER_DEPTH);
    buffer->count++;
    buffer_unlock(buffer);
    return 1U;
}

uint8_t message_buffer_pop(struct message_buffer_t *buffer,
                           struct message_buffer_record_t *record)
{
    if ((buffer == 0) || (record == 0)) return 0U;
    buffer_lock(buffer);
    if (buffer->count == 0U) {
        buffer_unlock(buffer);
        return 0U;
    }
    *record = buffer->records[buffer->read_index];
    buffer->read_index = (uint16_t)((buffer->read_index + 1U) % USBPD_MESSAGE_BUFFER_DEPTH);
    buffer->count--;
    buffer_unlock(buffer);
    return 1U;
}

void message_buffer_clear(struct message_buffer_t *buffer)
{
    if (buffer == 0) return;
    buffer_lock(buffer);
    buffer->read_index = 0U;
    buffer->write_index = 0U;
    buffer->count = 0U;
    buffer->dropped = 0U;
    buffer_unlock(buffer);
}

uint16_t message_buffer_count(const struct message_buffer_t *buffer)
{
    uint16_t count;
    if (buffer == 0) return 0U;
    buffer_lock(buffer);
    count = buffer->count;
    buffer_unlock(buffer);
    return count;
}

uint32_t message_buffer_dropped(const struct message_buffer_t *buffer)
{
    uint32_t dropped;
    if (buffer == 0) return 0U;
    buffer_lock(buffer);
    dropped = buffer->dropped;
    buffer_unlock(buffer);
    return dropped;
}
