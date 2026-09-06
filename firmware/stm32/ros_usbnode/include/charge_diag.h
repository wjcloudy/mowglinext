#ifndef CHARGE_DIAG_H
#define CHARGE_DIAG_H
#include <stdint.h>

#if CHARGE_DIAGNOSTICS
#define CHARGE_DIAG_RAW_COUNT 1024u
#define CHARGE_DIAG_CONTROL_COUNT 128u
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
typedef struct {
    uint32_t magic, version, raw_capacity, control_capacity;
    uint32_t raw_count, control_count, freeze_reason, trigger_tick;
    uint32_t missed_batches, max_gap_ms, raw_seq, control_seq;
    charge_diag_raw_t raw[CHARGE_DIAG_RAW_COUNT];
    charge_diag_control_t control[CHARGE_DIAG_CONTROL_COUNT];
} charge_diag_t;
extern volatile charge_diag_t charge_diag;
void ChargeDiag_RawBatch(uint32_t now, const uint16_t samples[20]);
void ChargeDiag_MissedBatch(void);
/* Foreground only: 1 ADC, 2 failed output, 3 restart budget exhausted. */
void ChargeDiag_Freeze(uint32_t now, uint32_t reason);
void ChargeDiag_Control(uint32_t now, uint16_t pwm, uint8_t state, uint8_t fault,
    float battery, float output, float input, float current,
    float current_before_offset, float temperature);
#endif
#endif
