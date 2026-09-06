# Blade direction commands and reversal guard

This change carries an explicit host direction request through the existing blade
driver. It does not alternate direction automatically. Current MowgliNext ROS2
`SetMowerEnabled` always sends direction zero; the existing MowerControl service
and USB blade packet can carry a different direction without a protocol change.

## Attribution and controller evidence

Adapted from Jeremy Salwen's original Mowgli firmware fix:
https://github.com/jeremysalwen/Mowgli/commit/dd6c01b64ac92e3c5f5edbea1112903c5ee82d35

Latch the direction in `BLADEMOTOR_Set` and use it in `blademotor_prepareMsg`,
which formerly overwrote the direction before every UART transmission. Generate
the additive checksum using the existing `crcCalc` function:

| Requested state | UART frame |
| --- | --- |
| Off | `55 AA 03 20 80 00 A2` |
| On, direction 0 | `55 AA 03 20 80 80 22` |
| On, direction nonzero | `55 AA 03 20 80 C0 62` |

The old commented reverse checksum `E2` was wrong. Jeremy's bench-test commit
reports that a 500B accepted `C0/62`, reached about 3300 RPM and reported no
protocol errors. It explicitly leaves physical rotation direction unverified:
https://github.com/jeremysalwen/Mowgli/commit/d78e2cc34cc1b03b03ada8bb7c1594c4bd0c7039

That bench auto-start/drive-reset code is not included here.

## Firmware guard: preserve when rebasing both LFP branches

- A change from the last transmitted running direction first sends OFF.
- Wait at least 1000 ms after UART accepts that OFF transmission, and require
  fresh, checksum-valid responses showing inactive, zero RPM and no error over
  at least 300 ms. Feedback older than 300 ms cannot qualify.
- Startup/cached zero RPM, a single old zero, malformed replies, active/nonzero
  replies and feedback gaps cannot release reversal. RX tracks interruptions
  even if a later good reply replaces them before the foreground checks.
- If communication never confirms stopping, continue sending OFF indefinitely.
  There is no timeout that forces the opposite running direction.
- An OFF request cancels pending reversal. An opposite-direction re-enable must
  establish a new stop interval. Repeated ON requests do not reset the interval.
- Changing the requested direction back during a pending reversal still waits
  for the stopped confirmation; it then uses the latest requested direction.
- Do not mutate the UART DMA request buffer from the setter or while TX is busy.
  A failed/busy OFF transmission does not start the guard's dwell timer.
- Existing emergency/heartbeat/idle blade gating, error stop, wheel control,
  charging fixes, ADC acquisition variants, temperature setup and protocol stay
  in their existing paths. Normal direction-zero starts are unchanged.

The first requested reverse start also requires the stop confirmation. These
guards reduce reliance on assumptions about spin-down; they cannot prove that
the controller's feedback or direction opcode matches physical shaft motion.

## Software validation

`python firmware/scripts/test_blade_reverse.py` (MSVC: add `--cc cl`) compiles
and runs the actual setter, frame builder, application loop and RX callback with
UART/clock stubs. Only hardware initialization is removed. The checksum function
is extracted from production `main.c`. Both 14- and 16-byte feedback formats are
covered, including guard boundaries, invalid/stale feedback, overwritten bad
replies, cancellation, TX failure/busy, IRQ-mask preservation and tick wrap.

## HARDWARE_REQUIRED before claiming reverse support

On the exact mower/controller being evaluated, in a supervised blades-removed
test, separately request each direction with a stop between starts. Confirm the
shaft/disc actually rotates in opposite directions; unsigned RPM or an accepted
UART response is insufficient evidence. Check that missing feedback leaves it
off and that the normal emergency/stop path cancels pending reversal. No deployed
mower has been tested or flashed as part of this build-only change.

ROS2 automatic alternation needs a separate policy and implementation after
physical support is confirmed. Do not port the upstream bench build's automatic
blade-start override into normal operation.
