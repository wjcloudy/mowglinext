# Blade NTC GPIO correction

The old modified ROS1 checkout explicitly enabled GPIOC and configured PC3 as
analogue. This was lost in the imported firmware. The current ADC still selects
channel 13 (PC3), but previously initialized PC2 and did not enable GPIOC first.
An earlier .118 register capture on firmware 1.9.113 confirmed PC3 digital-input
mode. The same source defect remained in installed 1.9.119 (905c84fc).

Restore the port clock before GPIO writes and select PC3 analogue/no-pull.
Preserve this initialization when rebasing either IRQ or DMA LFP over upstream.
PC2 is channel 12; do not change the blade channel to 12 to match the old comment.
No ADC sequence, acquisition timing, temperature equation, charge control,
current compensation, motor control, or wire protocol changes are required.

Run `python firmware/scripts/test_adc_gpio.py` (MSVC: add `--cc cl`) from the
repository root. It executes the production initialization prefix against a
clock-aware GPIO model for F103, F401 and F401 LFP. Existing acquisition tests
remove this function, so they did not detect the defect.

## HARDWARE_PENDING: .118 motor-start discrepancy

A correct GPIO setup does not prove that it explains the reported instantaneous
13 to 52 C jump. After deploying the matching 500B LFP diagnostic artifact:

1. Verify firmware readback and GPIOC MODER bits 7:6 = 3 (analogue), PUPDR bits
   7:6 = 0 (no pull). ADC ranks must remain [1, 2, 3, 7, 13].
2. Check charging, IMU/status traffic, ADC health and recorder progress.
3. In a supervised blades-removed test, capture raw NTC, reported temperature,
   current and RPM before/during/after wheel-only and blade-motor starts.
   Pass criterion: no immediate reversible temperature step with motor power;
   compare the settled cold reading with an independent thermometer.
4. If the step persists, measure PC3/sensor excitation relative to analogue
   ground during motor off/on transitions. Investigate interference, grounds,
   excitation and thermistor constants before applying any calibration offset.

Deployment logs and full STM32 backup belong in the named .118 backup directory
on the Pi. Preserve recorder evidence before reset; do not create Docker-image
archives for this firmware-only change. Motor-on validation is separate from
successful compilation, GPIO readback and idle telemetry.
