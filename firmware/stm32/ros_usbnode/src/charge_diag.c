#include "charge_diag.h"
#if CHARGE_DIAGNOSTICS
#include "main.h"

_Static_assert(sizeof(charge_diag_raw_t) == 16, "diagnostic raw ABI");
_Static_assert(sizeof(charge_diag_control_t) == 40, "diagnostic control ABI");
_Static_assert(sizeof(charge_diag_t) == 21552, "diagnostic dump ABI");
volatile charge_diag_t charge_diag = {
    .magic = 0x43484447u, .version = 1,
    .raw_capacity = CHARGE_DIAG_RAW_COUNT,
    .control_capacity = CHARGE_DIAG_CONTROL_COUNT
};

/* The DMA IRQ is the sole raw writer; foreground is the sole control writer.
 * Independent sequence counters let an external reader reject a moving dump.
 * No logging, allocation, register writes or charging decisions here. */
void ChargeDiag_RawBatch(uint32_t now, const uint16_t samples[20])
{
    if (charge_diag.freeze_reason) return;
    ++charge_diag.raw_seq;
    __DMB();
    for (unsigned row = 0; row < 4; ++row) {
        volatile charge_diag_raw_t *r = &charge_diag.raw[
            charge_diag.raw_count % CHARGE_DIAG_RAW_COUNT];
        r->tick = now;
        for (unsigned channel = 0; channel < 5; ++channel)
            r->adc[channel] = samples[row * 5 + channel];
        r->row = (uint16_t)row;
        ++charge_diag.raw_count;
    }
    __DMB();
    ++charge_diag.raw_seq;
}

void ChargeDiag_MissedBatch(void)
{
    if (!charge_diag.freeze_reason) ++charge_diag.missed_batches;
}

void ChargeDiag_Freeze(uint32_t now, uint32_t reason)
{
    uint32_t mask = __get_PRIMASK();
    __disable_irq();
    if (!charge_diag.freeze_reason) {
        charge_diag.trigger_tick = now;
        __DMB();
        charge_diag.freeze_reason = reason;
    }
    __set_PRIMASK(mask);
}

void ChargeDiag_Control(uint32_t now, uint16_t pwm, uint8_t state, uint8_t fault,
    float battery, float output, float input, float current,
    float current_before_offset, float temperature)
{
    static uint32_t previous, suspect_since;
    static uint8_t have_previous, suspect;
    if (charge_diag.freeze_reason) return;
    ++charge_diag.control_seq;
    __DMB();
    uint32_t gap = have_previous ? (uint32_t)(now - previous) : 0;
    previous = now;
    have_previous = 1;
    if (gap > charge_diag.max_gap_ms) charge_diag.max_gap_ms = gap;
    volatile charge_diag_control_t *r = &charge_diag.control[
        charge_diag.control_count % CHARGE_DIAG_CONTROL_COUNT];
    r->tick = now; r->gap_ms = gap; r->pwm = pwm;
    r->state = state; r->adc_fault = fault;
    r->battery = battery; r->output = output; r->input = input;
    r->current = current; r->current_before_offset = current_before_offset;
    r->temperature = temperature;
    r->missed_batches = charge_diag.missed_batches;
    ++charge_diag.control_count;

    /* Observation only: freeze after 250 ms of the measured .118 failure.
     * Gate on high requested PWM to exclude the ordinary startup ramp. */
    uint8_t failed = (state == 2 || state == 3) && pwm >= 1200
        && input >= 22.0f && battery > 20.0f
        && output < battery * 0.5f && current < 0.0f;
    uint32_t reason = fault ? 1u : 0u;
    if (failed) {
        if (!suspect) { suspect_since = now; suspect = 1; }
        if (!reason && (uint32_t)(now - suspect_since) >= 250u) reason = 2u;
    } else suspect = 0;
    if (reason) {
        charge_diag.trigger_tick = now;
        __DMB();
        charge_diag.freeze_reason = reason;
    }
    __DMB();
    ++charge_diag.control_seq;
}
#endif
