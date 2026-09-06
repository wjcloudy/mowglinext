# Charging-only fix plan for mower .118

6 September 2026. Plan only; no charging implementation or deployment performed.
Current installed firmware is the 1.9.113
diagnostic build `1bf1d5b8`. See [the diagnostic runbook](DIAGNOSTIC-RUNBOOK-118.md)
for the active watcher, capture paths and operator actions.

## Scope and evidence

Prevent brief dock-contact loss from reconnecting into retained high charging
PWM, and stop a failed charger remaining indefinitely at maximum requested duty.
Apply the shared charging behavior to both IRQ and DMA LFP branches, with a
diagnostic build for .118. Keep official upstream ROS2 images and protocol 6.

No drive, wheel PID, yaw, bumper, blade, emergency, motor-temperature or GNSS
changes. Preserve the LFP voltage/current envelope, electronics-current offset,
CC/CV/float targets, timer frequency, duty ceiling and dead time. Any future
temperature correction is a separate change and deployment decision.

Production-code reproduction established that the current LFP disconnect
debounce leaves regulation active for 20 consecutive low-input controller calls.
With simulated 10 ms calls, current -0.5 A, and starting PWM 1352, 100 ms input
loss raises duty to 1362; input recovery then bypasses the zero-duty startup
wait. Repeated 190 ms low / 10 ms high reaches the 1395 maximum. A continuous
200 ms low input resets PWM to zero. Actual normal controller calls are about
11 ms apart, and the input voltage is filtered, so these simulated timings are
not a physical detection bound. This proves PWM retention, not a hardware
overcurrent trip. The reported contact spike is supporting observation.

## Use the separate voltage measurements

| Measurement | Existing signal | Role in the fix |
| --- | --- | --- |
| Dock input, before the regulated charge rail | PA7 / ADC channel 7, `chargerInputVoltage` | Detect contact/power loss and qualify stable reconnection |
| Regulated charger output | PA2 / ADC channel 2, `charge_voltage` | Check whether requested charging actually produces output |
| Battery | PA3 / ADC channel 3, `battery_voltage` | Maintain battery limits and interpret output relative to the pack |
| Current, including electronics load | PA1 / ADC channel 1, `current` and pre-offset value | Regulation, excess-current backoff and confirmation of charge delivery |

Existing input filtering weights each new reading 0.5; LFP output filtering
weights it 0.1. Retain those filtered signals for ordinary control/telemetry,
but expose a separate fresh-sample protection path. Do not use the heavily
filtered output signal as the fast contact detector. Validate the signal
locations against .118's board measurements before setting electrical limits;
the voltage labels alone do not establish gate-driver or OCP behavior.

## Proposed implementation

### 1. Separate power inhibition from dock-status debounce

On the first valid acquired input sample below the existing 22 V loss threshold,
latch a charging inhibit and request the established zero-duty state. Preserve
debounce for logical dock state, but never let it authorize continued or rising
PWM after input loss has been detected. Clear the saved duty/ramp state so a
short interruption cannot resume the old high value. Invalid acquisition also
inhibits charging through the existing fail-safe path.

The acquisition side must remember a low sample even if the input has recovered
before the next foreground call. DMA must inspect completed samples since the
last visit, not only its latest row; IRQ must latch loss when channel 7 completes.
Use integer comparisons in the fast path. No logging, floating-point conversion,
RTC writes or full charger state-machine work in an ADC interrupt.

Make the fast inhibit and foreground PWM commit race-safe: a foreground write
must never restore a nonzero duty after an interrupt has inhibited charging.
Keep one explicit PWM-output abstraction and test the interrupt/commit ordering.
An acquisition-side register write, if used, is limited to forcing duty to zero.

Measure and document the actual worst-case detection-to-inhibit latency in each
variant. Current diagnostic DMA callbacks arrive in four-scan batches (about
8 ms); a nominal 2 ms ADC scan does not imply a 2 ms software cutoff. Shorter
contact events can remain invisible, and software is not a substitute for
hardware protection against sub-millisecond inrush or switching-current peaks.

### 2. Qualify reconnection, then restart gently

After any detected loss, require fresh, continuously valid input for a proposed
250 ms minimum holdoff; any new low reading restarts that timer. Use elapsed
time, not controller-call counts, and a distinct recovery threshold with
hysteresis chosen from measured input noise and available charger headroom.
Do not finalize that threshold from an assumed supply voltage.

Throughout the holdoff, duty remains zero. On recovery restart the charger ramp
from zero, never the previous duty. Preserve the existing slow upward rate in
real time even if protection runs faster. Keep active voltage/current limits
in force during the ramp, including when the battery is already near full.
Test that CC's normal PWM floor cannot bypass the startup ramp or inhibit.

Do not toggle the shared TF4 battery powerbus to handle brief contact bounce.
Keep any existing long-undock/startup behavior separate and verify Pi supply
continuity. Repeated interruptions must not trigger unbounded restart attempts:
use a bounded restart budget and latched fault on exhaustion. Proposed starting
budget for testing is three restart attempts per 60 seconds; finalize it after
the supervised contact test. Do not erase the budget on a brief input bounce.

### 3. Detect requested charge with failed output

Use all four measurements: valid dock input, appreciable requested PWM, battery
voltage, and output/current showing that charge is not being delivered. Require
a startup grace period and persistence so the normal ramp, full-battery float,
or ordinary disconnection cannot trigger the fault.

Start from the observed diagnostic predicate: CC/CV, PWM >=1200, input >=22 V,
battery >20 V, output < half battery voltage, and negative current for 250 ms.
Validate those conditions on .118 before promoting them to a control fault;
they describe the observed failure and are not universal charger thresholds.
On a confirmed failure, freeze pre-event history, inhibit charging and retain
a reason code. Do not remain saturated or report active charging indefinitely.
Preserve protocol compatibility: use existing charging status plus RAM
diagnostics, without promising a new GUI fault field that the protocol lacks.

Do not automatically retry an apparent hardware-output/OCP failure in the first
fix. First establish whether one supervised zero-duty dwell can clear it. Zero
CCR1 is the existing firmware's stop command; with complementary PWM it is not
necessarily both gates off and does not reproduce physical dock disconnection.
Verify gate behavior before changing output-enable or reset sequencing.

### 4. Retain current protection through both regulation modes

Give excess-current backoff priority over duty increases in CC and CV/float.
The current CV path can back off only one count per update; use the existing
tested proportional approach where appropriate, while respecting each mode's
active current target and the maximum runtime limit. Treat this as a separate
reviewable charging commit so contact-loss protection can be assessed alone.

Retain -0.20 A electronics compensation and the sensor's physical limitations.
Do not invent a hardware-trip threshold from the 1.8 A battery-charge target.
Fresh interval peaks can inform the guard and diagnostics, but the acquisition
path must actually capture those samples; a last-row read is not a peak detector.

## Tests and acceptance

1. Extend the production-C tests with input loss at high PWM in CC and CV,
   1/10/100/190 ms interruptions where sampled, repeated bounce, sustained
   undock, and return at different battery voltages. Assert no duty increase
   while inhibited, no previous-duty restart, a complete stable-input holdoff,
   bounded retries and correct elapsed-time behavior across tick wrap.
2. Cover DMA ring wrap/partial rows, a low sample followed by a high sample
   before foreground execution, IRQ sample publication, and an inhibit arriving
   during a foreground PWM commit. Validate the measured latency separately
   from the state-machine tests.
3. Exercise real ADC loss/errors and the known foreground-delay false latch.
   Correct freshness accounting in a separate charging-ADC commit if required;
   retain shutdown on genuine stale or failed acquisition.
4. Exercise failed-output detection against startup, normal CC, full-pack float,
   near-zero net current and input loss. Verify diagnostics freeze before
   clearing the evidence and that fault status does not claim active charging.
5. Run existing charger/ADC/diagnostic tests and firmware guards; build both
   LFP variants and check stock targets remain unchanged. Review the diff for
   the charging-only scope before deployment.
6. Preserve any current capture first. Flash a matched diagnostic fix to .118
   with the documented ITM-off OpenOCD procedure; retain the rollback image and
   verify readback. No fleet rollout or .119 flash is part of this plan.
7. Supervise a blades-removed charging test, observing input, output, battery,
   current, duty, inhibit/restart reason and timing. Reproduce contact loss with
   a suitable controlled disconnect setup rather than repeatedly making live
   sparking contacts. Check that reconnect begins at zero duty and ramps gently.
8. If a spike/trip persists, measure input inrush and gate/inductor behavior with
   suitable equipment. High-PWM reconnection and passive input-capacitor inrush
   are separate hypotheses; a contact spark alone does not distinguish them.
9. Complete a full CC/CV/float cycle and an overnight docked check. Acceptance:
   no retained-duty reconnect, no unexplained high-duty/no-output state, no
   battery drain from undetected loss of charging, and no new reset/ADC faults.

## Delivery order

First implement and test contact-loss inhibit/restart, then output-failure
detection, then the separately reviewed current-backoff/freshness corrections.
Keep these changes on an isolated charging branch until software checks pass
and .118 validation is complete. Port the shared fix to both LFP branches and
push them, retaining their IRQ/DMA acquisition differences. Do not fold the
motor-temperature GPIO work into this series.
