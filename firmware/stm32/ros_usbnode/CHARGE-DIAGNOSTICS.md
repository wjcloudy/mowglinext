# Charging diagnostic firmware for mower .118

Build target: `Yardforce500B_LFP_DIAG`, based on DMA LFP c2a0d4f6. This target is
for the STM32F401VC/500B and 8S LFP profile, not the standard 500 on mower .119.
It keeps the charge algorithm, 28.5 V / 1.8 A compiled ceilings, -0.20 A offset,
PWM/dead-time settings, bumper remap and emergency protections. Protocol remains
6 and official upstream ROS2 images remain usable. No retry/restart is added.

## Recorder

The `charge_diag` symbol holds a 21,552-byte versioned RAM record:

- 1024 raw scans: five ADC channels, measured at approximately 500 scans/second
  on .118 (about two seconds of history). TIM2's 1 ms compare toggles its output;
  rising-edge-only ADC triggering gives one scan every 2 ms, not every compare.
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

On .118, `st-flash` 1.7.0 with `--reset --freq=100` erased the application but
failed in its flash loader with USB timeouts. OpenOCD recovered the board and
independent readback matched the diagnostic binary exactly. Do not recommend
that st-flash invocation as the validated route for this mower.

The crucial OpenOCD command is **`itm ports off`**, after Cortex-M target creation
and before `init`. Port 0 is enabled automatically by target creation, even
without configuring a TPIU. On .118, merely leaving DBGMCU_CR/TRACE_IOEN clear
was insufficient: ITM TER=1, TCR=0x810009 and DEMCR=0x1000000 persisted, and the
bounded debug print stalled long enough to trip WWDG at DRIVEMOTOR_10MS.
Clearing ITM restored charging and IMU telemetry without a physical power cycle.
An attachment with `itm ports off` was subsequently checked: TER remained zero.
See https://openocd.org/doc/html/Architecture-and-Core-Commands.html (ITM commands).

The recovery procedure is captured in `remote_upload/yardforce500b_no_trace.cfg`.
It avoids TPIU/SWO, pauses watchdogs only while halted, writes/verifies flash and
resets the CPU with SYSRESETREQ. After verifying board identity, backup and image
hash, and stopping the ROS2 bridge for maintenance:

```sh
sudo openocd -c "set FIRMWARE /absolute/path/firmware.bin" \
  -f remote_upload/yardforce500b_no_trace.cfg
```

No NRST wiring is required for SYSRESETREQ. Do not invoke the SWO viewer: PB3 is
also the IMU clock. After flashing, independently verify readback and require
live IMU, battery/current and recorder counters before completing maintenance.
The installed diagnostic image remains build commit 1bf1d5b8 / version 1.9.113;
this later documentation/config change does not change its binary.

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
After installation on .118, raw/control counters advanced, missed batches and
freeze reason stayed zero, and maximum observed control gap was 11 ms. IMU and
charging telemetry recovered after disabling ITM, without a physical power cycle.
An actual charge-failure freeze still needs field validation.

Do not enable automatic recovery yet. First capture a failure, then separately
test one controlled zero-PWM dwell/restart. Zero duty is not equivalent to both
complementary outputs off or physical removal of dock input.
