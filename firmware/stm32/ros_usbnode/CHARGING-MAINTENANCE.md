# Maintaining the LFP charging protection overlay

Implemented 6 September 2026 on `codex/lfp-charging-contact-fix`. This document
describes code, not proof of electrical protection or an overnight charge test.
The .118 deployment manifest identifies the exact built commit and image hash.
Keep this file with the source when updating from upstream `dev`.

## Why these changes exist

The older 20-controller-call undock debounce retained high PWM during short
contact breaks. Production-C reproduction showed PWM 1352 rising during a
100 ms break and being reused at reconnection. A previous .118 failure also
had healthy dock-input voltage, low charger-output voltage, negative current
and maximum requested duty. The input and output rails are separate signals.

The overlay is deliberately charging-only. Do not mix drive/yaw/blade/bumper,
emergency or thermistor GPIO work into a merge or rollback of these changes.
The compiled 28.5 V / 1.8 A ceiling, 27.5 V / 0.4 A float, -0.20 A electronics
compensation, TIM1 frequency/dead time and maximum duty remain unchanged.
Protocol 6 and upstream ROS2 images remain compatible.

## Files and contracts to preserve across upstream merges

| File | Contract |
| --- | --- |
| `include/charge_protection.h` | Raw input thresholds, timing/retry constants, RAM state ABI and acquisition hooks |
| `src/adc.c` | Publish every completed input sample to `Charger_InputSample`; DMA inspects all four rows of each half; errors and lost/moving halves inhibit |
| `src/charger.c` | Acquisition can only stop PWM; foreground alone releases after qualification; final PWM write rechecks the inhibit with interrupts masked |
| `include/adc.h` | `ADC_ChargingFaulted()` remains available without diagnostics so real faults latch the guard |
| `src/charge_diag.c`, `include/charge_diag.h` | Preserve pre-stop history and explicit freeze reasons; recorder freezing must not disable the charging guard |
| `firmware/scripts/charge_diag_dump.py` | Accept reasons 1 ADC, 2 failed output, 3 exhausted restart budget; address must come from matching ELF |
| `firmware/scripts/test_charge_protection.py` | Contact/restart/output regression and simulated input-IRQ versus PWM-commit race |
| `firmware/scripts/test_adc_charging.py`, `test_charge_diag.py` | Production acquisition ordering, errors, ring boundaries and foreground-delay regression |

The GPIO/ADC channel map and temperature conversion are intentionally unchanged.
The separate thermistor-PC3 issue remains a separate work item.

## Input loss and restart

The fast hook compares raw ADC channel 7 against 1706 (about 22 V using the
existing 3.3 V, 16:1 conversion). A low sample latches inhibition and immediately
writes CCR1=0 if TIM1 has been initialized. It cannot release inhibition.
A lost/moving DMA batch or acquisition error also inhibits, since missing data
cannot establish uninterrupted input. A later high row in the same batch must
not erase the observed loss.

Recovery requires continuously fresh samples >=1784 (about 23 V) for at least
250 ms. The 1 V hysteresis is below the observed .118 dock input (~28.4 V),
and deliberately avoids re-enabling near the 22 V loss boundary. Values between
the thresholds do not qualify recovery. Input age >30 ms invalidates it.
These constants must be revisited if the divider/reference/dock supply changes.

After qualification the controller discards saved duty, enters CONNECTED at
zero, retains the existing >100 ms wait, then ramps from zero at no more than
one count per 11 ms. Delayed calls do not catch up multiple increments. The
normal regulating floor must not create a 0-to-39 jump during a reduction.
Short contact recovery does not switch TF4 off or recalibrate away electronics
load. The UI charging flag is cleared while inhibited; the old undock debounce
constant no longer delays power inhibition.

The first connection after boot is not a retry. Subsequent qualified starts
allow three attempts in a 60-second window. A fourth attempt latches fault 3;
the window does not reset on contact bounce. The fault remains until MCU reboot.
Normal power loss can therefore require operator intervention after repeated
unstable contacts. Do not silently replace this with unlimited attempts.

## Concurrency and sampling limits

The input ISR may interrupt all foreground charging calculations. Only the
final masked block may commit a nonzero PWM, and it MUST recheck both inhibit
and fault in that block. Restoring the prior PRIMASK is mandatory; do not
unconditionally enable interrupts. Never move logging or RTC writes into the
input ISR. Do not enable ITM/SWO to debug this path on .118.

DMA LFP now enables half/full/error interrupts even without the flight recorder.
Each half contains four five-channel scans, approximately 8 ms of data on .118.
Contact inhibition is therefore bounded by batch delivery plus IRQ latency for
sampled events, not by the ~2 ms scan period alone. Pulses falling between input
samples may be invisible. This is not hardware cycle-by-cycle current protection.

The acquisition timestamp advances in the DMA IRQ. A foreground gap >30 ms
alone must not manufacture an ADC fault if acquisition remained fresh and a
new snapshot succeeds. Genuine missing progress/errors still latch; repeated
incoherent snapshots cannot renew the processed-data deadline. Preserve stock
behavior behind the non-LFP conditional.

For the IRQ LFP branch, keep its per-channel sampler and call the same input
hook when channel 7 completes. Do not replace its ADC implementation with the
DMA file. The shared charger and protection header should remain identical.

## Current and failed-output protection

CC retains its existing proportional backoff. CV now uses the same 1/2/6/16
step sizing relative to its effective float-current limit; upward adjustment
cannot occur during an excess-current reduction. Both respect runtime limits.
No new instantaneous hardware-OCP threshold is inferred from the battery-current
sensor. It includes Pi/electronics load and may miss switching-current spikes.

After a two-second startup grace, CC/CV duty >=1200, input >=22 V, battery >20 V,
output <half battery voltage and negative current persisting 250 ms latches
fault 2. A recovered reading cancels the pending suspect interval. The diagnostic
records pre-stop duty before inhibition; charging status goes inactive. A full
battery with near-zero current and normal output is not a failure.

Faults do not retry automatically. CCR1=0 is the established firmware stop
command; complementary PWM means it is not necessarily both gates disabled,
and it is not physical dock removal. Gate waveforms and possible input-capacitor
inrush remain hardware validation items. Save a fault capture before resetting.

## Verification and upstream update checklist

1. Make the upstream merge in an isolated worktree. Review `adc.c`, `charger.c`,
   TIM1/ADC initialization and any new foreground PWM writers manually.
2. Keep the hooks/critical section above; keep acquisition variants distinct.
   Look for changed DMA half sizes, rank order, interrupt ownership or divider
   scaling. The current DMA hook assumes exactly 8 scans / 5 channels.
3. Run as the project user, never root:

```sh
python firmware/scripts/test_charge_protection.py
python firmware/scripts/test_lfp_charger.py
python firmware/scripts/test_adc_charging.py
python firmware/scripts/test_charge_diag.py
python firmware/scripts/board_defaults_parity.py
python firmware/scripts/protocol_version_guard.py --check
```

On Windows use the MSVC developer shell and `--cc cl` for C harnesses. The
diagnostic harness applies to the DMA tree. Build LFP and the stock targets;
build `Yardforce500B_LFP_DIAG` for .118 evidence collection. Commit before the
final build so its firmware identity is clean and reproducible.

4. Back up installed flash and capture RAM before any reset. Use the verified
   ITM-off helper, verify image readback, then restart the upstream bridge.
   Start a new watcher with the NEW ELF address and a new output directory.
5. Verify fresh IMU/voltage/current and advancing recorder counters. Read
   `charge_protection` from the matching ELF: inhibited=0, fault=0 and one
   qualified start are expected with stable dock power.
6. A normal startup check does not validate contact-bounce waveforms or an
   overnight cycle. Supervise those separately, using a suitable disconnect
   fixture rather than deliberately making sparking contacts. Keep rollback
   binaries and the deployment report on the Pi.
