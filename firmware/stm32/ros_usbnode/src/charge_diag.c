#include "charge_diag.h"
#if CHARGE_DIAGNOSTICS
#include "main.h"

_Static_assert(sizeof(charge_diag_raw_t) == 16, "diagnostic raw ABI");
_Static_assert(sizeof(charge_diag_control_t) == 40, "diagnostic control ABI");
_Static_assert(sizeof(charge_diag_context_t) == 40, "diagnostic context ABI");
_Static_assert(sizeof(charge_diag_detail_t) == 84, "diagnostic detail ABI");
_Static_assert(sizeof(charge_diag_t) == 29688, "diagnostic dump ABI");
volatile charge_diag_t charge_diag = {
    .magic = 0x43484447u, .version = CHARGE_DIAG_VERSION,
    .raw_capacity = CHARGE_DIAG_RAW_COUNT,
    .control_capacity = CHARGE_DIAG_CONTROL_COUNT,
    .slow_capacity = CHARGE_DIAG_SLOW_COUNT,
    .event_capacity = CHARGE_DIAG_EVENT_COUNT
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

void ChargeDiag_Context(const charge_diag_context_t *context)
{
    if (!charge_diag.freeze_reason) charge_diag.context = *context;
}

/* Only describes this firmware's CKD=DIV1, linear DTG timer configuration.
 * This is not a gate waveform measurement or a hardware fault diagnosis. */
static uint8_t pulse_suppressed(const charge_diag_detail_t *d)
{
    uint32_t dtg = d->context.bdtr & 255u;
    return d->context.arr && dtg < 128u
        && (uint32_t)d->sample.pwm + dtg >= d->context.arr + 1u;
}

static void record_detail(const charge_diag_control_t *sample)
{
    charge_diag_detail_t d = { .sample = *sample, .context = charge_diag.context };
    if (!charge_diag.slow_count || (uint32_t)(sample->tick - charge_diag.slow[
            (charge_diag.slow_count - 1u) % CHARGE_DIAG_SLOW_COUNT].sample.tick) >= 1000u) {
        charge_diag.slow[charge_diag.slow_count % CHARGE_DIAG_SLOW_COUNT] = d;
        ++charge_diag.slow_count;
    }
    if (!charge_diag.event_count) d.reason = 1u;
    else {
        charge_diag_detail_t p = charge_diag.events[
            (charge_diag.event_count - 1u) % CHARGE_DIAG_EVENT_COUNT];
        if (p.sample.state != d.sample.state) d.reason |= 2u;
        if (p.context.losses != d.context.losses || p.context.starts != d.context.starts
                || p.context.inhibited != d.context.inhibited
                || p.context.protection_fault != d.context.protection_fault) d.reason |= 4u;
        if (p.context.target != d.context.target || p.context.current_limit != d.context.current_limit
                || p.context.float_target != d.context.float_target) d.reason |= 8u;
        if (pulse_suppressed(&p) != pulse_suppressed(&d)) d.reason |= 16u;
    }
    if (d.reason) {
        charge_diag.events[charge_diag.event_count % CHARGE_DIAG_EVENT_COUNT] = d;
        ++charge_diag.event_count;
    }
}

void ChargeDiag_Control(uint32_t now, uint16_t pwm, uint8_t state, uint8_t fault,
    float battery, float output, float input, float current,
    float current_before_offset, float temperature)
{
    static uint32_t previous, suspect_since;
    static uint8_t have_previous, suspect;
    if (charge_diag.freeze_reason) return;
    if (!charge_diag.control_count) have_previous = suspect = 0;
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
    charge_diag_control_t sample = *r;
    record_detail(&sample);

    /* Capture the onset, before the existing half-battery/250ms fault trigger
     * overwrites it. First require 500ms of established output at high duty.
     * Reason 4 only freezes evidence: it never stops or restarts the charger. */
    uint32_t early_reason = 0;
    uint8_t active = (state == 2 || state == 3) && pwm >= 1200
        && input >= 23.0f && battery > 20.0f;
    if (!active) charge_diag.early_armed = charge_diag.early_suspect = 0;
    else if (charge_diag.early_armed < 2u) {
        if (output >= battery - 0.5f) {
            if (!charge_diag.early_armed) {
                charge_diag.early_armed = 1;
                charge_diag.early_since = now;
            }
            if ((uint32_t)(now - charge_diag.early_since) >= 500u) charge_diag.early_armed = 2;
        } else charge_diag.early_armed = 0;
    } else if (output < battery - 1.0f && current < 0.0f) {
        if (!charge_diag.early_suspect) {
            charge_diag.early_suspect = 1;
            charge_diag.early_first_tick = now;
        }
        if ((uint32_t)(now - charge_diag.early_first_tick) >= 44u) early_reason = 4u;
    } else charge_diag.early_suspect = 0;

    /* Observation only: freeze after 250 ms of the measured .118 failure.
     * Gate on high requested PWM to exclude the ordinary startup ramp. */
    uint8_t failed = (state == 2 || state == 3) && pwm >= 1200
        && input >= 22.0f && battery > 20.0f
        && output < battery * 0.5f && current < 0.0f;
    uint32_t reason = fault ? 1u : early_reason;
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
