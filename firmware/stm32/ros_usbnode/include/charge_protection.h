#ifndef CHARGE_PROTECTION_H
#define CHARGE_PROTECTION_H
#include <stdint.h>

/* CLOUDY LFP charging overlay: keep these hooks when merging upstream ADC and
 * charger changes. See CHARGING-MAINTENANCE.md for ordering and timing contracts.
 * Raw thresholds use the existing 3.3 V, 16:1 input conversion. */
#if BOARD_YARDFORCE500B_LFP
#define CHARGE_INPUT_LOSS_RAW 1706u       /* <=22.0 V */
#define CHARGE_INPUT_RECOVER_RAW 1784u    /* >=23.0 V; 1 V hysteresis */
#define CHARGE_INPUT_STABLE_MS 250u
#define CHARGE_INPUT_FRESH_MS 30u
#define CHARGE_RESTART_WINDOW_MS 60000u
#define CHARGE_RESTART_LIMIT 3u
enum { CHARGE_FAULT_NONE, CHARGE_FAULT_ADC, CHARGE_FAULT_OUTPUT,
       CHARGE_FAULT_RESTART_LIMIT };
typedef struct {
    uint32_t version, inhibited, fault, input_seen, qualified;
    uint32_t stable_since, last_input_ms, raw_input, losses, starts;
    uint32_t restart_window_ms, restarts, window_active, restart_pending;
} charge_protection_t;
extern volatile charge_protection_t charge_protection;
/* ADC writer: every completed input sample, including historical DMA rows.
 * Never releases an inhibit. A zero-only timer write is allowed in this hook. */
void Charger_InputSample(uint16_t raw, uint32_t now);
void Charger_InputInvalid(void);
#endif
#endif
