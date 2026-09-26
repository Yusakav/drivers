#ifndef PD_TRACE_H
#define PD_TRACE_H

#include "usbpd_types.h"

#define PD_TRACE_DEPTH 16U

enum pd_trace_dir_e { PD_TRACE_RX = 0U, PD_TRACE_TX = 1U };
enum pd_trace_flag_e { PD_TRACE_GOODCRC = 1U, PD_TRACE_ERROR = 2U, PD_TRACE_RESET = 4U };

struct pd_trace_record_t {
    uint32_t timestamp_ms;
    uint8_t raw[USBPD_TRACE_FRAME_LEN];
    struct {
        uint8_t length : 6;
        uint8_t direction : 1;
        uint8_t goodcrc : 1;
        uint8_t sop : 3;
        uint8_t error : 1;
        uint8_t reset : 1;
        uint8_t reserved : 3;
    } bits;
};

void pd_trace_reset(void);
void pd_trace_push(uint8_t direction, uint8_t sop, const uint8_t *raw,
                   uint8_t len, uint32_t timestamp_ms, uint8_t flags);
uint8_t pd_trace_pop(struct pd_trace_record_t *record);
void pd_trace_service(uint8_t max_records);

#endif
