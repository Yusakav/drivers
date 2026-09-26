#include "pd_trace.h"
#include <string.h>
#include <stdio.h>
#include "ch32x035.h"
#include "debug.h"

struct pd_trace_queue_t {
    struct pd_trace_record_t record[PD_TRACE_DEPTH];
    volatile uint8_t read_index;
    volatile uint8_t write_index;
    volatile uint8_t count;
};

static struct pd_trace_queue_t g_trace;

static void trace_irq_lock(void)
{
    /* CH32X035 uses QingKe's interrupt CSR instead of standard mstatus.MIE. */
    __disable_irq();
}

static void trace_irq_unlock(void)
{
    __enable_irq();
}

void pd_trace_reset(void)
{
    trace_irq_lock();
    memset(&g_trace, 0, sizeof(g_trace));
    trace_irq_unlock();
}

void pd_trace_push(uint8_t direction, uint8_t sop, const uint8_t *raw,
                   uint8_t len, uint32_t timestamp_ms, uint8_t flags)
{
    struct pd_trace_record_t *record;

    if ((raw == 0) && (len != 0U)) {
        return;
    }
    if (len > USBPD_TRACE_FRAME_LEN) {
        len = USBPD_TRACE_FRAME_LEN;
        flags |= PD_TRACE_ERROR;
    }

    trace_irq_lock();
    if (g_trace.count == PD_TRACE_DEPTH) {
        g_trace.read_index = (uint8_t)((g_trace.read_index + 1U) % PD_TRACE_DEPTH);
        g_trace.count--;
    }
    record = &g_trace.record[g_trace.write_index];
    memset(record, 0, sizeof(*record));
    record->timestamp_ms = timestamp_ms;
    record->bits.length = len;
    record->bits.direction = (direction != 0U);
    record->bits.goodcrc = ((flags & PD_TRACE_GOODCRC) != 0U);
    record->bits.sop = sop;
    record->bits.error = ((flags & PD_TRACE_ERROR) != 0U);
    record->bits.reset = ((flags & PD_TRACE_RESET) != 0U);
    if (len != 0U) {
        memcpy(record->raw, raw, len);
    }
    g_trace.write_index = (uint8_t)((g_trace.write_index + 1U) % PD_TRACE_DEPTH);
    g_trace.count++;
    trace_irq_unlock();
}

uint8_t pd_trace_pop(struct pd_trace_record_t *record)
{
    if (record == 0) {
        return 0U;
    }
    trace_irq_lock();
    if (g_trace.count == 0U) {
        trace_irq_unlock();
        return 0U;
    }
    *record = g_trace.record[g_trace.read_index];
    g_trace.read_index = (uint8_t)((g_trace.read_index + 1U) % PD_TRACE_DEPTH);
    g_trace.count--;
    trace_irq_unlock();
    return 1U;
}

void pd_trace_service(uint8_t max_records)
{
    struct pd_trace_record_t record;
    uint8_t i;
    for (i = 0U; i < max_records && pd_trace_pop(&record); ++i) {
        uint8_t j;
        PRINT("[PD][%lu][%s][SOP%u]%s%s:",
              (unsigned long)record.timestamp_ms,
              record.bits.direction ? "TX" : "RX", record.bits.sop,
              record.bits.goodcrc ? "[CRC]" : "",
              record.bits.error ? "[ERR]" : "");
        for (j = 0U; j < record.bits.length; ++j) {
            PRINT(" %02X", record.raw[j]);
        }
        PRINT("\r\n");
    }
}
