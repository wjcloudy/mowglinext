#ifndef __BOARD_DEFAULTS_H
#define __BOARD_DEFAULTS_H

/*******************************************************************************
 * SINGLE SOURCE OF TRUTH for the board-independent, operator-facing firmware
 * defaults (battery/charge envelope, emergency-sensor timeouts, onboard-IMU
 * tilt threshold).
 *
 * WHY THIS FILE EXISTS
 *   These values used to be hand-copied in TWO places that silently drifted:
 *     - the committed board.h (what firmware CI compiles), and
 *     - board.h.template's {{.Field}} placeholders (what the GUI renders +
 *       flashes), whose fill values are the FlashBoardComponent.tsx form
 *       defaults.
 *   The two disagreed on 5 SAFETY values (charge cutoff, one-wheel-lift
 *   timeout, ...). This header is now the ONE place the blessed defaults live.
 *   Both consumers include it:
 *     - board.h  #includes it and defines NONE of these itself -> CI builds
 *       exactly these blessed values.
 *     - board.h.template renders `#define X {{.X}}` (the GUI value) and THEN
 *       includes this file: every default below is `#ifndef`-guarded, so a
 *       GUI-supplied value WINS and an absent one falls back to the blessed
 *       default. (This is the "baked #ifndef fallbacks the GUI overrides"
 *       model.)
 *   A CI drift guard (firmware/scripts/board_defaults_parity.py) asserts the
 *   FlashBoardComponent.tsx form defaults equal the values below, so the
 *   board.h-vs-GUI drift can never silently recur.
 *
 * CHANGING A VALUE
 *   Edit it HERE, then update the matching FlashBoardComponent.tsx `default={}`
 *   (the guard will fail until they agree). Do NOT re-hardcode any of these in
 *   board.h or inline in the template.
 *
 * SAFETY
 *   Every value here affects physical behaviour (charging envelope, e-stop
 *   response, tilt cutoff). Treat any change as safety-critical and
 *   robot-verify it. This file NEVER enables I_DONT_NEED_MY_FINGERS.
 ******************************************************************************/

#if BOARD_YARDFORCE500B_LFP
#undef MAX_PWM_VALUE
#undef MIN_PWM_VALUE
#undef CURRENT_OFFSET
#undef BATTERY_CAPACITY
#undef MAX_CHARGE_CURRENT
#undef MAX_CHARGE_VOLTAGE
#undef LIMIT_VOLTAGE_150MA
#undef BAT_CHARGE_CUTOFF_VOLTAGE
#undef MAX_FLOAT_CV_VOLTAGE
#undef FLOAT_CV_CURRENT
#undef CHARGE_END_LIMIT_CURRENT
#undef CV_ENTRY_DEBOUNCE_CYCLES
#undef CV_EXIT_HYSTERESIS
#undef CV_VOLTAGE_DEADBAND
#undef V_BATT_SMOOTH_ALPHA
#undef V_CHARGE_SMOOTH_ALPHA
#undef MIN_DOCKED_VOLTAGE
#undef UNDOCK_DEBOUNCE_CYCLES
#undef MIN_BATTERY_VOLTAGE
#undef MIN_CHARGE_CURRENT
#undef LOW_BAT_THRESHOLD
#undef LOW_CRI_THRESHOLD
/* ===========================================================================
 * CLOUDY LFP charging profile (transposed from the ROS1 500B mainboard build)
 * Pack: 8S LiFePO4, ~4.8 Ah  (stock firmware targets 7S Li-ion, ~2.8 Ah).
 * These compiled ceilings survive GUI template rendering. Runtime USB limits
 * may lower the charge envelope, but cannot raise it above this profile.
 * =========================================================================== */

/// PWM safety ceiling (TIM1 period is 1400; stock cap was 1350).
/// An 8S LFP at ~28.5V on the ~29-30V rail has little buck headroom, so it
/// needs more duty to reach full current ("different charge-current/PWM curve").
#define MAX_PWM_VALUE 1395
/// PWM floor kept while still actively regulating (was the magic value 39)
#define MIN_PWM_VALUE 39
/// Fixed current-sensor offset [A]. Replaces auto-zeroing at dock, which is wrong
/// here because the Pi/electronics still draw current at the assumed "zero" point.
/// NOTE: re-measure on the actual hardware if the current reading looks biased.
#define CURRENT_OFFSET -0.20f
/// Usable pack capacity [Ah] for SOC coulomb counting (LFP voltage-based SOC is useless)
#define BATTERY_CAPACITY 4.8f

/// nominal max charge current [A] (~0.375C on a 4.8 Ah pack)
#define MAX_CHARGE_CURRENT 1.8f
/// Max charge-rail voltage allowed [V] (8S * 3.56 V/cell)
#define MAX_CHARGE_VOLTAGE 28.5f
/// CC->CV transition threshold [V] - kept for reference; the runtime
/// charge_end_voltage actually drives the transition (default = cutoff below)
#define LIMIT_VOLTAGE_150MA 28.5f
/// Default max battery voltage [V] - initial value of the runtime charge_end_voltage
#define BAT_CHARGE_CUTOFF_VOLTAGE 28.5f
/// CV/float battery ceiling [V] (8S * 3.44 V/cell). Effective target = min(this, charge_end_voltage).
#define MAX_FLOAT_CV_VOLTAGE 27.5f
/// CV/float current limit [A] - fixed, replaces the stock MAX_CHARGE_CURRENT/10.
/// CLOUDY raised 0.30 -> 0.40: 300mA couldn't cover the docked standby draw, so the pack
/// sagged off the 27.5V float until the 26.5V CC fallback re-engaged (slow sawtooth). This is
/// a cap; in CV the loop targets the float VOLTAGE and only draws what's needed to hold it.
#define FLOAT_CV_CURRENT 0.40f
/// We consider the battery full when CV current drops below this [A] (LFP has a flat tail)
#define CHARGE_END_LIMIT_CURRENT 0.25f
/// CLOUDY CC<->CV anti-latch. An 8S LFP has a very flat SoC curve and low ESR, so the
/// terminal voltage under ~1.8 A charge current sits ~1.5 V above the resting/SoC voltage.
/// Without these guards that loaded voltage trips CC->CV early and the one-way latch
/// (charger_set_end_voltage is never called) floats the pack at FLOAT_CV_CURRENT forever
/// while it is only part full (observed: stuck at ~0.3 A with the pack at 26.9 V).
/// Number of ChargeController cycles (~10 ms each) the CC->CV trip must hold before latching.
#define CV_ENTRY_DEBOUNCE_CYCLES 50
/// CV->CC fallback: only drop back to bulk CC if the (smoothed) pack voltage sags this far
/// below the CV trip - i.e. the pack is genuinely under-charged, NOT just floating. Must sit
/// well below where a full LFP floats (~27.5-28V), else normal float ripple bounces CV<->CC
/// and the pack hunts on its steep top-of-charge knee (observed 27.5<->28.7V at ~1Hz). [V]
#define CV_EXIT_HYSTERESIS 2.0f
/// CLOUDY CV voltage deadband: don't nudge PWM while within +/- this of the float target, so
/// the CV loop stops limit-cycling around it (the LFP knee turns a tiny ripple into a big
/// terminal-voltage swing). [V]
#define CV_VOLTAGE_DEADBAND 0.2f
/// CLOUDY ADC IIR smoothing weight on each NEW sample (~10 ms apart) for the voltages the
/// charge loop acts on. Lower = more smoothing / more lag. The PWM-switched charge rail and
/// the LFP pack's load-induced voltage swing are noisy, so smooth them hard (stock was
/// battery 0.2, charge rail 0.8).
#define V_BATT_SMOOTH_ALPHA   0.05f  /* ~190 ms time constant */
#define V_CHARGE_SMOOTH_ALPHA 0.1f   /* ~90 ms time constant (rail is the noisiest) */
// if charger-input voltage is greater than this assume we are docked [V]
#define MIN_DOCKED_VOLTAGE 22.0f
/// CLOUDY: ChargeController cycles (~10ms each) the charger input must stay below
/// MIN_DOCKED_VOLTAGE before we declare "undocked" and drop to IDLE (PWM 0). Debounced so a
/// transient input sag (e.g. a CC current overshoot, or a noisy reading) can't drop PWM to 0.
#define UNDOCK_DEBOUNCE_CYCLES 20
// if voltage is lower this assume battery is disconnected [V]
#define MIN_BATTERY_VOLTAGE 5.0f

// if current is greater than this assume the battery is charging [A]
#define MIN_CHARGE_CURRENT 0.1f
#define LOW_BAT_THRESHOLD 24.0f /* 8S LFP ~3.0 V/cell */
#define LOW_CRI_THRESHOLD 23.0f /* 8S LFP ~2.88 V/cell */

// Preserve the tilt threshold and bumper timings of the custom 500B hardware.
#undef IMU_ONBOARD_INCLINATION_THRESHOLD
#define IMU_ONBOARD_INCLINATION_THRESHOLD 0x2C
#define BUMP_MILLIS_WHILE_MOWING 100
#define BUMP_MILLIS_WHILE_DOCKING 500
#define BUMP_REVERSE_MILLIS 1000
#endif

/* --- Battery / charge envelope ---------------------------------------------
 * Blessed 2026-07-18 to the (higher) board.h values. NOTE: this RAISES the
 * charge envelope vs what shipped via the GUI form defaults before this change
 * — safety-critical, robot-verify required. */
#ifndef MAX_CHARGE_CURRENT
#define MAX_CHARGE_CURRENT 1.2f
#endif
#ifndef MAX_CHARGE_VOLTAGE
#define MAX_CHARGE_VOLTAGE 29.4f
#endif
// DEAD: no firmware code reads LIMIT_VOLTAGE_150MA (defined only here + in the
// template). NOT runtime-migrated (P4) — a runtime knob would do nothing. Left
// in place; removing the dead #define is separate cleanup.
#ifndef LIMIT_VOLTAGE_150MA
#define LIMIT_VOLTAGE_150MA 28.8f
#endif
// Initial charge target. The LFP controller additionally caps this target
// to the runtime voltage ceiling on every tick; runtime limits never raise
// the compiled profile ceiling.
#ifndef BAT_CHARGE_CUTOFF_VOLTAGE
#define BAT_CHARGE_CUTOFF_VOLTAGE 29.2f
#endif

/* Fixed battery constants (not operator-configurable, identical in both former
 * copies — centralised here to end the duplication). */
#ifndef CHARGE_END_LIMIT_CURRENT
#define CHARGE_END_LIMIT_CURRENT 0.08f
#endif
#ifndef MIN_DOCKED_VOLTAGE
#define MIN_DOCKED_VOLTAGE 20.0f
#endif
#ifndef MIN_BATTERY_VOLTAGE
#define MIN_BATTERY_VOLTAGE 5.0f
#endif
#ifndef MIN_CHARGE_CURRENT
#define MIN_CHARGE_CURRENT 0.1f
#endif
#ifndef LOW_BAT_THRESHOLD
#define LOW_BAT_THRESHOLD 25.2f /* near 20% SOC */
#endif
#ifndef LOW_CRI_THRESHOLD
#define LOW_CRI_THRESHOLD 23.5f /* near 0% SOC */
#endif

/* --- Emergency-sensor timeouts [ms] ----------------------------------------
 * ONE_WHEEL_LIFT blessed 2026-07-18 to 2000 (board.h value); the GUI form
 * default was 10000 and is being brought into line. Safety-relevant. */
#ifndef ONE_WHEEL_LIFT_EMERGENCY_MILLIS
#define ONE_WHEEL_LIFT_EMERGENCY_MILLIS 2000
#endif
#ifndef BOTH_WHEELS_LIFT_EMERGENCY_MILLIS
#define BOTH_WHEELS_LIFT_EMERGENCY_MILLIS 1000
#endif
#ifndef TILT_EMERGENCY_MILLIS
#define TILT_EMERGENCY_MILLIS 500 /* mechanical + accelerometer detection */
#endif
#ifndef STOP_BUTTON_EMERGENCY_MILLIS
#define STOP_BUTTON_EMERGENCY_MILLIS 100
#endif
#ifndef PLAY_BUTTON_CLEAR_EMERGENCY_MILLIS
#define PLAY_BUTTON_CLEAR_EMERGENCY_MILLIS 2000
#endif

/* --- Onboard-IMU (LIS3DH) tilt-interrupt threshold -------------------------
 * Feeds lis3dh_int1_gen_threshold_set() (src/i2c.c). Now operator-configurable
 * via the template ({{.ImuOnboardInclinationThreshold}}), so the firmware
 * CLAMPS it to a safe envelope: any value outside [0x2C, 0x40] is rejected and
 * forced back to the vetted default 0x38, so a bad/hostile config can never
 * weaken tilt detection. 0x38 is stricter than stock firmware's 0x2C (which
 * allows more inclination); 0x2C is the most-permissive value we still treat as
 * shipped-safe, hence the lower bound. Direction-agnostic: extremes on EITHER
 * side fall back to 0x38. SAFETY: robot-verify any change to the envelope.
 * Mirrors the runtime pkt_set_*_t clamp discipline, at compile time. */
#ifndef IMU_ONBOARD_INCLINATION_THRESHOLD
#define IMU_ONBOARD_INCLINATION_THRESHOLD 0x38
#endif
#if (IMU_ONBOARD_INCLINATION_THRESHOLD < 0x2C) || \
    (IMU_ONBOARD_INCLINATION_THRESHOLD > 0x40)
#undef IMU_ONBOARD_INCLINATION_THRESHOLD
#define IMU_ONBOARD_INCLINATION_THRESHOLD 0x38
#endif

#endif /* __BOARD_DEFAULTS_H */
