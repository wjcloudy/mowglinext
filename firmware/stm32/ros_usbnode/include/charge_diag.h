#ifndef CHARGE_DIAG_H
#define CHARGE_DIAG_H
#include <stdint.h>

#if CHARGE_DIAGNOSTICS
#define CHARGE_DIAG_RAW_COUNT 1024u
#define CHARGE_DIAG_CONTROL_COUNT 128u
#define CHARGE_DIAG_SLOW_COUNT 64u
#define CHARGE_DIAG_EVENT_COUNT 32u
#define CHARGE_DIAG_VERSION 2u
typedef struct {
    uint32_t tick; /* DMA batch service time, NOT individual conversion time */
    uint16_t adc[5]; /* current, output, battery, input, NTC */
    uint16_t row; /* row within the four-scan DMA half */
} charge_diag_raw_t;
typedef struct {
    uint32_t tick, gap_ms;
    uint16_t pwm;
    uint8_t state, adc_fault;
    float battery, output, input, current, current_before_offset, temperature;
    uint32_t missed_batches;
} charge_diag_control_t;
/* Foreground snapshots only. Timer registers report configuration/requested
 * compare values, not measured gate waveforms. No register writes here. */
typedef struct {
    float target, current_limit, float_target;
    uint32_t ccr1, arr, bdtr, ccer, losses, starts;
    uint16_t cv_count;
    uint8_t inhibited, protection_fault;
} charge_diag_context_t;
typedef struct {
    charge_diag_control_t sample;
    charge_diag_context_t context;
    uint32_t reason; /* 1 initial, 2 state, 4 protection/contact, 8 limits, 16 duty boundary */
} charge_diag_detail_t;
typedef struct {
    uint32_t magic, version, raw_capacity, control_capacity;
    uint32_t raw_count, control_count, freeze_reason, trigger_tick;
    uint32_t missed_batches, max_gap_ms, raw_seq, control_seq;
    charge_diag_raw_t raw[CHARGE_DIAG_RAW_COUNT];
    charge_diag_control_t control[CHARGE_DIAG_CONTROL_COUNT];
    /* ABI v2 appends to the unchanged v1 header/rings. One-minute trend plus
     * sparse transition history survives far longer than the fast rings. */
    uint32_t slow_capacity, event_capacity, slow_count, event_count;
    uint32_t early_armed, early_since, early_suspect, early_first_tick;
    charge_diag_context_t context;
    charge_diag_detail_t slow[CHARGE_DIAG_SLOW_COUNT];
    charge_diag_detail_t events[CHARGE_DIAG_EVENT_COUNT];
} charge_diag_t;
extern volatile charge_diag_t charge_diag;
void ChargeDiag_RawBatch(uint32_t now, const uint16_t samples[20]);
void ChargeDiag_MissedBatch(void);
/* Foreground only: 1 ADC, 2 failed output, 3 legacy restart budget,
 * 4 early output loss (observation only; NOT a charger protection fault). */
void ChargeDiag_Freeze(uint32_t now, uint32_t reason);
void ChargeDiag_Context(const charge_diag_context_t *context);
void ChargeDiag_Control(uint32_t now, uint16_t pwm, uint8_t state, uint8_t fault,
    float battery, float output, float input, float current,
    float current_before_offset, float temperature);
#endif
#endif
