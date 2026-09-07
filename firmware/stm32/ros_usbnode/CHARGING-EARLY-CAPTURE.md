# Early charging capture: .118, 7 September 2026

**1390 trial update:** following the observation-only 1.9.123 deployment, the
user selected a five-count reduction from 1395 to 1390 to preserve near-full
headroom. This revision changes only the LFP maximum PWM, retaining ABI 2 and
all voltage/current/offset/restart settings. It still suppresses the
complementary pulse (11 counts before 40-count dead time); it is a controlled
peak-duty experiment, not a fix for pulse suppression. Compare charging
stability and reachable battery voltage. The findings and 1395 reproductions
below describe the original incident/build; current tests use the 1390 ceiling.

The original observation-only diagnostic extends the DMA `Yardforce500B_LFP_DIAG` image installed from
`a84ecfe5` (1.9.122). It changes observation only, with no changes to charge
limits, PWM regulation, restart policy, ADC acquisition, motors or protocol.
It retains that base's temperature, blade-reversal and I2C-recovery fixes.
The interrupt-ADC LFP branch is a different build; do not confuse the two.

## What the captured failure establishes

Evidence remains on .118 under
`/home/pi/mower-backups/192.168.1.118/incidents/2026-09-07_072924_charging-stopped/`.
The frozen `recorder.bin` SHA256 is
`1f6b97bf088f6392027c98c57a44e8629c82e2f5a43e4f26376cfd3d0f3da15c`.

The event was approximately 04:11:48 BST, estimated from uptime, not an MCU RTC
timestamp. PWM was **constant at 1395** throughout the retained window, in
state 2 (CC). Input remained 28.470–28.494 V, battery 28.359–28.383 V and net
current was negative. Filtered output fell from 16.99 to 13.62 V. Even the first
raw sample already showed low output: the late trigger missed the beginning.
Control gaps were 11 ms with no recorded missed batches or ADC faults.
Later output was essentially zero and the existing failed-output protection
had latched PWM off. No retained positive spike proves a hardware current trip.

## Why near-full can still request maximum PWM

`test_charge_near_full.py` exercises the production C controller with supplied
measurements, not an electrical battery model. It reproduces:

- Battery 28.36 V, input 28.48 V and negligible current: CC remains active and
  duty reaches 1395 because the default target is 28.5 V.
- Alternating 28.49/28.51 V: the 50-consecutive-cycle qualification continually
  resets. At 11 ms/cycle, reaching float requires about 550 ms above the target.
- Established CV at its 27.5 V target holds duty even with negative net current.
- Redocking discards the previous float state and begins a new CC cycle.

Displayed SOC does not decide the charge state. The captured input voltage
also leaves little measured headroom for the 28.5 V target. Calibration,
voltage drops and actual battery state still need checking before changing
charge termination. Preserve the current offset: the Pi load shares the sensor.

## Maximum-duty finding

TIM1 uses ARR=1400, CKD=DIV1 and linear dead time DTG=40. The PWM period is
1401 counts. At CCR1=1395, the reference complementary pulse is only 6 counts;
the delayed active-high complementary pulse is suppressed. At the stock cap
1350, 51 counts remain before dead time, leaving an 11-count pulse.

This follows the complementary-output timing in
[ST RM0368, section 12.3.11, figures 73–75](https://www.st.com/resource/en/reference_manual/DM00096844-.pdf).
It makes sustained maximum duty a concrete suspect. It does **not** establish
gate-driver bootstrap requirements, actual transistor waveforms or an
overcurrent trip. Those need driver/circuit evidence or scope measurements.
The original 1.9.123 image deliberately retained 1395 to observe the existing failure; any
lower-cap experiment should be a separately identified build and checked for
adequate charging headroom. Do not raise the cap or remove dead time.

## Recorder ABI 2

The new record is 29,688 bytes (8,136 more than ABI 1). The original 1,024 raw
scans and 128 fast control samples retain their layouts and rates. Added:

- 64 snapshots, at most one per second (roughly the last minute).
- 32 sparse events for state, contact/protection, runtime-limit or calculated
  complementary-pulse-boundary changes. These survive long steady periods;
  frequent transitions can overwrite them.
- Effective bulk/float voltage targets, current limit, CV qualification count,
  contact starts/losses, inhibit/fault state and TIM1 CCR1/ARR/BDTR/CCER.

Event reason bits: 1 initial, 2 state, 4 contact/protection, 8 limits, 16 duty
boundary. The timer boundary calculation assumes the current CKD=DIV1 and
DTG<128 configuration. Requested PWM and committed CCR1 are recorded separately;
neither is a measurement of the pin. Context is a foreground observation and
may straddle an interrupt or host-limit update; it is not an atomic hardware
snapshot. Acquisition and input-loss IRQ paths are unchanged.

New freeze reason **4** captures early output loss. It first requires high duty
(>=1200), valid input (>=23 V), battery >20 V and output within 0.5 V of battery
for 500 ms. Once armed, output more than 1 V below battery with negative net
current for 44 ms freezes evidence. Low duty or lost input disarms it; recovered
output/current cancels a pending trigger. Reasons 1/2 retain the original ADC
and late output-loss capture. Protection continues after any diagnostic freeze.

Reason 4 is an observation, **not a protection fault**. A load transient can
freeze it without a later charger failure, and the one-shot recorder will then
miss a subsequent event. Reboot rearms it; redocking does not. Sampling still
cannot resolve switching spikes. Filter delay can put the physical beginning
outside even this earlier capture. Hardware timing/RAM use need field validation.

## Use and next decision

Run as the normal project user from repository root:

```sh
python firmware/scripts/test_charge_near_full.py
python firmware/scripts/test_charge_diag.py
python firmware/scripts/test_charge_protection.py
python firmware/scripts/test_adc_charging.py
cd firmware/stm32/ros_usbnode
pio run -e Yardforce500B_LFP_DIAG
```

MSVC native tests accept `--cc cl`. Commit before the final build so its reported
version is clean; retain the exact ELF, binary hash and symbol address together.
Use the existing no-trace flashing procedure in `CHARGE-DIAGNOSTICS.md` only for
a separately scheduled maintenance flash. This document is not a flash record.

Leave the old Pi watcher paused: it uses an old ELF address. Avoid repeated SWD
attachments while mowing. After a failure, retain the ROS readings and use the
matching ELF address for a single frozen dump with the updated decoder:

```sh
python3 charge_diag_dump.py capture --address ADDRESS_FROM_MATCHING_ELF \
  --directory /home/pi/mower-backups/192.168.1.118/incidents/UNIQUE_NAME/mcu-capture
```

The decoder supports both ABIs and exports `slow.csv` and `events.csv` for ABI 2.
Its memory bounds now use the actual
[STM32F401VC 64 KiB SRAM](https://www.st.com/en/microcontrollers-microprocessors/stm32f401vc.html).
The repository's conservative PlatformIO board declaration still reports 48 KiB;
this change does not enlarge that declaration or the linker region.

Compare state transitions and actual limits first, then the duty-boundary event
against raw output/current collapse and input stability. If the charger stays
in CC at the cap before losing output, the next controlled experiment is a
separate lower-duty-cap image, with charging headroom and waveform verification.
Any full-charge/float change should follow the measured target/headroom results.
Keep hardware trip recovery and charge-state changes separate so results can
be attributed. Do not introduce automatic retries of the existing output fault.
