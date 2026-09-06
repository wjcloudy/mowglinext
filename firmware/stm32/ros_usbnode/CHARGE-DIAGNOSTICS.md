# Charging diagnostic firmware for mower .118

Build target: `Yardforce500B_LFP_DIAG`, based on DMA LFP c2a0d4f6. This target is
for the STM32F401VC/500B and 8S LFP profile, not the standard 500 on mower .119.
It keeps the charge algorithm, 28.5 V / 1.8 A compiled ceilings, -0.20 A offset,
PWM/dead-time settings, bumper remap and emergency protections. Protocol remains
6 and official upstream ROS2 images remain usable. No retry/restart is added.

## Recorder

The `charge_diag` symbol holds a 21,552-byte versioned RAM record:

- 1024 raw scans: five ADC channels, acquired at the existing nominal 1 kHz.
  DMA half/full interrupts copy four completed scans per batch. The timestamp
  is IRQ service time, not a claim of exact individual conversion time.
- 128 control updates: actual gap, PWM, charger state, ADC fault, battery/output/
  input voltages, filtered current before/after compensation and temperature.
- Header: counts, maximum observed control gap, missed-batch indicator, freeze
  reason and sequence counters. Missed batches are a lower-bound indication;
  multiple DMA wraps during a long interrupt delay cannot be counted exactly.

The recorder freezes on an ADC fault (reason 1), or after 250 ms of the observed
failed-output condition (reason 2): CC/CV, PWM >=1200, input >=22 V, battery >20 V,
output below half battery voltage and negative current. This predicate only
freezes RAM; it does not shut down or restart charging. Other failure shapes may
not freeze this first diagnostic version. Reboot erases/rearms the history;
redocking alone does not. Capture before rebooting.

No telemetry print or floating-point conversion occurs in the DMA recorder IRQ.
The IRQ acknowledges only completion flags and retains TC progress in a sticky
software flag for the original ADC health logic. Error flags remain for that
logic. Moving/overwritten halves are discarded and marked. Diagnostics add IRQ
load, so timing impact must still be checked on hardware. They do not guarantee
capture of sub-millisecond switch-current spikes; a scope may still be needed.

The known foreground-delay ADC false latch is deliberately unchanged for this
observation build. A frozen reason-1 dump distinguishes it from reason 2.

## Build and test

As the ordinary project user:

```sh
cd firmware/stm32/ros_usbnode
pio run -e Yardforce500B_LFP_DIAG
```

From repository root (MSVC developer shell: append `--cc cl`):

```sh
python firmware/scripts/test_charge_diag.py
python firmware/scripts/test_adc_charging.py
python firmware/scripts/test_lfp_charger.py
python firmware/scripts/board_defaults_parity.py
python firmware/scripts/protocol_version_guard.py --check
```

The production-C harness covers DMA half ordering, overwritten/delayed batches,
sticky TC and retained errors, ring wrap, freeze/debounce and tick wrap, plus
unchanged ADC safety. The decoder is tested against a binary written by that C
code. These are software tests, not electrical validation.

## Flash/reset route

The likely remembered flag is `st-flash --reset`. The official manual says it
resets before and after flashing:
https://github.com/stlink-org/stlink/blob/testing/doc/man/st-flash.md

Candidate command after verifying the board identity, backup and binary hash,
and stopping the ROS2 bridge for the maintenance operation:

```sh
sudo st-flash --reset write firmware.bin 0x08000000
```

This is a prepared route, not a command run as part of building this artifact.
Confirm installed st-flash options before use. `--connect-under-reset` is a
different option for connecting to a difficult target and requires the probe's
NRST connection; do not assume that wire is present.

Earlier .118 trouble involved the IMU clock on PB3/TRACESWO and OpenOCD trace
handling. Do not invoke PlatformIO's SWO viewer. A plain reset is not a proven
repair for an already disturbed IMU. Prefer testing st-flash without enabling
trace, verify flash readback, then confirm USB telemetry and IMU samples resume
without a physical cycle. This route has not yet been validated on .118.

## Read diagnostics without disturbing the MCU

Find the address from the **matching** ELF (also placed in the artifact manifest):

```sh
arm-none-eabi-nm -S -n firmware.elf | grep ' charge_diag$'
```

On the Pi, copy `firmware/scripts/charge_diag_dump.py` and use that address:

```sh
python3 charge_diag_dump.py capture --address ADDRESS_FROM_MANIFEST \
  --directory /home/pi/mower-backups/192.168.1.118/diagnostics/UNIQUE_CAPTURE_NAME --watch
```

It checks a small header every five seconds and saves/decodes the frozen history
when a fault occurs. Omit `--watch` for a single header check. It uses OpenOCD
`mem_ap` only: no Cortex-M target, TPIU/SWO, halt, reset, flash or register write.
All evidence remains on the Pi. The output directory must be new. No watcher is
started merely by building this firmware.

The full dump's header must match before/after readout, be frozen and have even
sequence counters; otherwise decoding fails. Outputs are `recorder.bin`,
`decoded.json`, `raw.csv`, `control.csv`, the read configuration and OpenOCD log.
Read-only attachment was validated with the previous firmware; the new recorder
address, IRQ timing and freeze behavior still need validation after installation.

Do not enable automatic recovery yet. First capture a failure, then separately
test one controlled zero-PWM dwell/restart. Zero duty is not equivalent to both
complementary outputs off or physical removal of dock input.
