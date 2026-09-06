# Mower .118: diagnostic processes and next steps

Updated 6 September 2026. This is the operational record for 192.168.1.118,
not the standard 500 on .119. Full backups and captured evidence stay on the Pi.

## Installed firmware and observation

- Installed: 1.9.113, protocol 6, DMA LFP diagnostic build `1bf1d5b8`.
- Binary: 91,856 bytes, SHA256
  `47a77f948c942f58936629251ea4bd223e77545ff1bae5d38400ff3de53ebf4a`.
- The diagnostic adds recording, not a charging fix or automatic retry.
  Existing 28.5 V / 1.8 A ceilings and -0.20 A electronics compensation remain.
- Last watcher check: 2026-09-06 10:54 UTC, PID 15525 as pi, counters advancing,
  freeze reason 0, missed batches 0, maximum controller gap 11 ms.
  This is a dated check, not a guarantee that the process remains running.
- Live ROS status shortly afterward: firmware 1.9.113, blade RPM 0,
  motor temperature 12.35 C. The recorded WWDG reset cause is from the earlier
  flashing/debug startup issue; its presence alone does not mean another reset.

## Processes and evidence locations

The MCU maintains a 21,552-byte RAM recorder at `0x20000010`, valid only for
the matching installed ELF. It retains 1,024 five-channel raw ADC scans
(about two seconds at 500 Hz) and 128 controller updates (about 1.4 seconds).
Raw records include the NTC reading; controller records include temperature,
PWM, state, current, voltage, ADC faults and controller timing.

The Pi process `python3 -u .../charge_diag_dump.py capture ... --watch` checks
the header every five seconds using OpenOCD's `mem_ap` target. It neither halts
nor resets the MCU, enables trace, writes MCU registers, or changes charging.
It saves the frozen recorder and exits. It is a detached process, not a service:
it does **not** automatically restart after Pi reboot. MCU reboot erases/rearms
the RAM recorder; redocking does not rearm an already frozen recorder.

Deployment directory:

```text
/home/pi/mower-backups/192.168.1.118/deployments/2026-09-06_lfp-diag_1bf1d5b8_092928Z/
```

It contains firmware/ELF/manifest, pre-flash backups, verification logs,
`charge_diag_dump.py`, `recorder-watch.json` and `recorder-watch.log`.
`CURRENT-DEPLOYMENT.txt` at the backup root points to it.

Current watcher output directory:

```text
/home/pi/mower-backups/192.168.1.118/diagnostics/lfp-diag-20260906T094406Z/
```

While waiting, this contains `header.bin`, `read.cfg` and `openocd.log`.
A successful fault capture adds `before.bin`, `recorder.bin`, `after.bin`,
`decoded.json`, `raw.csv` and `control.csv`. The decoder requires a frozen,
stable header and even sequence counters. A header file alone is not a capture.

## What the operator should do on recurrence

1. Leave the mower docked and powered. Initially avoid redocking, restarting
   containers, rebooting the Pi/MCU or reflashing.
2. Record the time, displayed current/SOC, whether a motor had just run, and
   report the dropout. Wait at least one watcher interval for automatic saving.
3. Have the saved capture and live charger state checked before recovery.
   If anything becomes unusually hot or smells wrong, disconnect power;
   preserving evidence is secondary.

Freeze reason 1 means the firmware's ADC fault latch fired. Reason 2 means
CC/CV requested PWM >=1200 with input >=22 V and battery >20 V, while output
remained below half battery voltage and current negative for 250 ms.
Other failure patterns may not freeze this version. If no capture exists,
inspect live telemetry and registers without resetting; do not treat absence
of a freeze as proof that charging is healthy.

## Checking or restarting the Pi watcher

Run as pi. Inspect the process and log first; do not start a second OpenOCD
user while one is already active:

```sh
deployment=/home/pi/mower-backups/192.168.1.118/deployments/2026-09-06_lfp-diag_1bf1d5b8_092928Z
pgrep -af '[c]harge_diag_dump.py'
tail -n 5 "$deployment/recorder-watch.log"
```

Only if the watcher has exited, the installed firmware still matches, and
the evidence has been checked, start a new watch in a new directory:

```sh
capture=/home/pi/mower-backups/192.168.1.118/diagnostics/lfp-diag-$(date -u +%Y%m%dT%H%M%SZ)
nohup python3 -u "$deployment/charge_diag_dump.py" capture \
  --address 0x20000010 --directory "$capture" --watch \
  > "$deployment/recorder-watch-$(date -u +%Y%m%dT%H%M%SZ).log" 2>&1 < /dev/null &
```

This requires passwordless `sudo -n openocd`, as configured on this Pi.
Keep the new PID/output path with the deployment records. Restarting the watcher
does not rearm the MCU recorder. Avoid the SWO viewer and ordinary Cortex-M
debug attachment; see [CHARGE-DIAGNOSTICS.md](CHARGE-DIAGNOSTICS.md) for ITM-off
flashing and why `st-flash --reset` was not the successful route on this mower.

## Route after capturing a failure

The contact-bounce finding and proposed implementation are now detailed in
[the charging-only fix plan](CHARGING-FIX-PLAN.md).

1. Preserve and checksum the raw/decoded files, matching ELF, ROS power/status,
   reset cause and timestamps before any recovery.
2. For reason 1, distinguish a real acquisition failure from the reproduced
   foreground-delay false latch. Fix freshness accounting without weakening
   loss-of-ADC shutdown, and test both ADC branches.
3. For reason 2, inspect pre-event current, input/output voltages, PWM and
   control-loop gaps. Separate dock-input loss, regulation overshoot and hardware
   output shutdown. The ADC recorder cannot exclude sub-millisecond current
   spikes; a scope/current probe may be needed to establish hardware OCP.
4. After evidence is safe, test one supervised recovery. First establish whether
   a controlled zero-PWM dwell actually resets the charger. Zero duty is not
   necessarily both complementary outputs off or dock power removal. Do not
   add an unbounded automatic retry that repeatedly hits hardware protection.
5. Validate a targeted fix through motor load transitions, dock reconnects,
   CC/CV/float and a full charge cycle; then apply the appropriate shared fix
   to both LFP branches. Keep the upstream ROS2 images/configuration.

The temperature discrepancy is a separate investigation. Do not hide it with
an arbitrary +4 C display offset or disturb this charging capture to test it.
