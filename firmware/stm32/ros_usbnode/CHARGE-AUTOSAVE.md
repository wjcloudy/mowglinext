# Standalone .118 charge evidence saver

The Pi service `mowgli-charge-autosave` is separate from the MowgliNext checkout,
GUI and updater. It subscribes using the existing ROS container through standard
input; no image, compose, application config or container file is patched.
Run as pi; only the existing `sudo -n openocd` hardware access is privileged.

Source: `firmware/scripts/charge_diag_autosave.py` and matching systemd unit.
Installed tools live at `/home/pi/mower-backups/192.168.1.118/tools/charge-autosave/`.
The service follows `CURRENT-DEPLOYMENT.txt`, requiring a verified DMA diagnostic
deployment, matching binary/ELF checksums, manifest ABI/address, and a matching
fresh reported firmware version/protocol. Future manual flashes must update
that manifest/pointer; upstream non-diagnostic images will fail the guard.

Changed acquisition stamps renew status/power/wheel freshness, not cached callback
arrival. Mission receipt supplies only intent/liveness (that message has no
acquisition stamp). After 15 seconds of fresh IDLE, blade-off and stationary wheel
observations, it reads the recorder at most once a minute. It avoids routine SWD
reads while mowing and refuses other OpenOCD/decoder processes. Reads use only
the existing mem_ap decoder: no Cortex-M target, halt/reset, ITM/SWO or writes.
The shared maintenance lock is `diagnostics/autosave/openocd.lock` under the
backup root. Stop the service and take that lock before flashing/debugging.

Frozen captures go under `incidents/<UTC>_charge-autosave/` with raw/decoded data,
firmware manifest, matching deployment location, Pi boot ID, ROS observation and
running container identities. Duplicate frozen blobs are suppressed across service
restarts. `diagnostics/autosave/` holds latest status/header/probe log and daily
five-second telemetry JSONL. All evidence stays on the Pi; no Docker image backup
is created. Logs currently remain until manually archived/removed.

```sh
systemctl status mowgli-charge-autosave
journalctl -u mowgli-charge-autosave -n 30 --no-pager
cat /home/pi/mower-backups/192.168.1.118/diagnostics/autosave/latest-header.json
# Pause before maintenance; resume only after the new manifest is verified.
sudo systemctl stop mowgli-charge-autosave
sudo systemctl start mowgli-charge-autosave
```

It never resets/retries charging, clears emergencies or commands motors. A reason-4
freeze can be a harmless early transient; once frozen the MCU recorder cannot
record a later failure until reboot. A power cycle before the next eligible read
can still lose evidence. No frozen trace does not prove charging is healthy.

## Hardware acceptance

HARDWARE_PENDING: the deployment manifest on .118 defines the exact firmware
commit/image/ELF/address, and each capture records the then-running host images.
Software guard tests and a live stationary probe do not prove long-term capture
or charging stability. With blades removed, wheels secured and accessible cutoff,
supervise dock reconnection, then at least one day charging on this exact baseline.
Pass requires stable charge regulation and preserved limits; if it fails, a stable
frozen dump with matching provenance must be saved before reset/battery shutdown.
Check counters, sample gaps, input/output rails, PWM and faults in the saved trace.
Do not deliberately provoke an electrical trip. Waveforms/current spikes need
separate safe measurements; 1380 remains an unproven duty-ceiling experiment.
