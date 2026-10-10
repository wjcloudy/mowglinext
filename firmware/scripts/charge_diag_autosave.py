#!/usr/bin/env python3
"""Standalone .118 Pi observer: save frozen RAM evidence, never reset/retry.

Runs as pi. Uses the existing ROS container only to subscribe, without copying
files into it. SWD reads are restricted to fresh IDLE/stationary observations.
The deployment manifest and matching ELF own the recorder identity/address.
"""
import argparse
from datetime import date, datetime, timedelta, timezone
import hashlib
import json
import math
from pathlib import Path
import re
import selectors
import stat
import subprocess
import tempfile
import time

TELEMETRY_DAYS = 7
TELEMETRY_BYTES = 100_000_000  # 100 MB total, independent of the age limit.
TELEMETRY_CHUNK_BYTES = 10_000_000
TELEMETRY_NAME = re.compile(r'(\d{4}-\d{2}-\d{2})_telemetry(?:-(\d{4}))?\.jsonl')


def telemetry_files(state):
    """Only our directly owned routine logs; never walk incident/deployment trees."""
    state = state.absolute()
    if state.resolve() != state:
        raise ValueError('Telemetry directory must not pass through a symlink')
    files = []
    for path in state.iterdir():
        match = TELEMETRY_NAME.fullmatch(path.name)
        if not match or not stat.S_ISREG(path.lstat().st_mode):
            continue  # Symlinks, directories, captures and all other names survive.
        try:
            day = date.fromisoformat(match[1])
        except ValueError:
            continue
        files.append((day, int(match[2] or 0), path, path.stat().st_size))
    return sorted(files, key=lambda f: (f[0], f[1]))


def retain_telemetry(state, today=None, max_bytes=TELEMETRY_BYTES):
    """Keep seven UTC calendar days, then discard oldest logs to meet the cap."""
    today = today or datetime.now(timezone.utc).date()
    cutoff = today - timedelta(days=TELEMETRY_DAYS - 1)
    files = telemetry_files(state)
    total = sum(f[3] for f in files)
    removed = []
    for day, sequence, path, size in files:
        if day < cutoff or total > max_bytes:
            # Recheck the direct file immediately before unlinking. Do not follow
            # links, recurse, truncate other files, or prune firmware/fault data.
            if path.parent.resolve() != state.resolve() or not stat.S_ISREG(path.lstat().st_mode):
                raise ValueError('Telemetry cleanup target changed')
            path.unlink()
            total -= size
            removed.append(path.name)
    return dict(days=TELEMETRY_DAYS, max_bytes=max_bytes, retained_bytes=total, removed=removed)


def append_telemetry(state, row):
    today = datetime.now(timezone.utc).date()
    files = [f for f in telemetry_files(state) if f[0] == today]
    line = json.dumps(row)+'\n'
    index = files[-1][1] if files else 0
    if files and files[-1][3] + len(line.encode('utf-8')) > TELEMETRY_CHUNK_BYTES:
        index += 1
    if index > 9999:
        raise ValueError('Too many telemetry chunks for one UTC day')
    suffix = '' if index == 0 else f'-{index:04d}'
    path = state / f'{today}_telemetry{suffix}.jsonl'
    if path.is_symlink() or (path.exists() and not path.is_file()):
        raise ValueError('Unsafe telemetry append target')
    with path.open('a', encoding='utf-8') as f:
        f.write(line)
    report = retain_telemetry(state, today)
    (state / 'retention.json').write_text(json.dumps(dict(utc=utc(), **report), indent=2)+'\n')
    if report['removed']:
        print('Routine telemetry cleanup: '+json.dumps(report), flush=True)

# These topics provide observation timestamps. Only a changed acquisition stamp
# renews their freshness; cached republication cannot keep the guard alive.
# HighLevelStatus has no stamp: its receipt is intent/liveness, not acquisition.
OBSERVER = r'''
import json, time
import rclpy
from rclpy.qos import qos_profile_sensor_data
from mowgli_interfaces.msg import Status, Power, HighLevelStatus
from nav_msgs.msg import Odometry
rclpy.init()
n = rclpy.create_node('lfp_charge_evidence_observer')
latest, changed, stamps = {}, {}, {}
def record(key, msg, values):
    stamp = getattr(msg, 'stamp', getattr(getattr(msg, 'header', None), 'stamp', None))
    if stamp is not None:
        identity = (stamp.sec, stamp.nanosec)
        if identity == (0, 0): return
        if stamps.get(key) != identity:
            changed[key] = time.monotonic()
            stamps[key] = identity
        values['stamp'] = identity
    else:
        changed[key] = time.monotonic()
    latest[key] = values
subs = [
 n.create_subscription(Status, '/hardware_bridge/status', lambda m: record('status', m,
  dict(fw=m.firmware_version, protocol=m.firmware_protocol_version,
       compatible=m.firmware_compatible, mow=m.mow_enabled, rpm=m.mower_motor_rpm,
       reset=m.reset_cause_name, charging=m.is_charging)), qos_profile_sensor_data),
 n.create_subscription(Power, '/hardware_bridge/power', lambda m: record('power', m,
  dict(battery=m.v_battery, output=m.v_charge, current=m.charge_current)), qos_profile_sensor_data),
 n.create_subscription(HighLevelStatus, '/behavior_tree_node/high_level_status',
  lambda m: record('mission', m, dict(state=m.state_name, substate=m.sub_state_name)), 10),
 n.create_subscription(Odometry, '/wheel_odom', lambda m: record('wheel', m,
  dict(speed=m.twist.twist.linear.x, yaw_rate=m.twist.twist.angular.z)), qos_profile_sensor_data)]
last, started = 0, time.monotonic()
# Bound the lifetime even if the host Docker client disappears during a Pi
# service restart; docker exec client termination alone need not kill its child.
while rclpy.ok() and time.monotonic()-started < 180:
 rclpy.spin_once(n, timeout_sec=0.1)
 now = time.monotonic()
 if now-last >= 1:
  print(json.dumps(dict(values=latest, age={k:now-v for k,v in changed.items()})), flush=True)
  last = now
n.destroy_node()
rclpy.shutdown()
'''


def utc():
    return datetime.now(timezone.utc).strftime('%Y-%m-%dT%H:%M:%SZ')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def eligible(snapshot, manifest):
    values, ages = snapshot.get('values', {}), snapshot.get('age', {})
    if any(ages.get(k, math.inf) > 3 for k in ('status', 'power', 'mission', 'wheel')):
        return False
    s, w = values['status'], values['wheel']
    return (s['fw'] == manifest['version'] and s['protocol'] == manifest['protocol']
            and s['compatible'] and not s['mow'] and s['rpm'] == 0
            and values['mission']['state'] == 'IDLE'
            and math.isfinite(w['speed']) and math.isfinite(w['yaw_rate'])
            and abs(w['speed']) < 0.001 and abs(w['yaw_rate']) < 0.001)


def deployment(root):
    path = Path((root / 'CURRENT-DEPLOYMENT.txt').read_text().strip()).resolve()
    if path.parent != (root / 'deployments').resolve() or not (path / 'FLASH-VERIFIED').exists():
        raise ValueError('No verified deployment under this mower backup root')
    m = json.loads((path / 'manifest.json').read_text())
    if (m['unit'] != '192.168.1.118' or m['target'] != 'Yardforce500B_LFP_DMA_DIAG'
            or m['charge_diag_abi'] != 2 or m['charge_diag_size'] != 29688):
        raise ValueError('Unsupported unit/target/recorder identity')
    if digest(path / 'firmware.bin') != m['binary_sha256'] or digest(path / 'firmware.elf') != m['elf_sha256']:
        raise ValueError('Deployment image/ELF checksum mismatch')
    address = int(m['charge_diag_address'], 0)
    if address % 4 or not 0x20000000 <= address <= 0x20010000 - m['charge_diag_size']:
        raise ValueError('Invalid matching recorder address')
    return path, m


def capture(root, path, manifest, snapshot, state):
    # The service holds the same cooperative lock that maintenance must take.
    # Refuse other OpenOCD users too; never attach alongside a flasher/debugger.
    other = subprocess.run(['pgrep', '-f', '[o]penocd|[c]harge_diag_dump.py'],
                           capture_output=True, text=True)
    if other.stdout.strip():
        print('Capture deferred: another debugger/recorder is active', flush=True)
        return
    with tempfile.TemporaryDirectory(prefix='probe-', dir=state) as temp:
        out = Path(temp) / 'mcu-capture'
        result = subprocess.run(['python3', str(path / 'charge_diag_dump.py'), 'capture',
                                 '--address', manifest['charge_diag_address'],
                                 '--directory', str(out)], capture_output=True, text=True, timeout=100)
        (state / 'last-probe.log').write_text(result.stdout + result.stderr)
        result.check_returncode()
        from charge_diag_dump import header
        h = header((out / 'header.bin').read_bytes())
        (state / 'latest-header.json').write_text(json.dumps(dict(utc=utc(), deployment=str(path), **h), indent=2)+'\n')
        if not (out / 'decoded.json').exists():
            return
        key = manifest['binary_sha256'] + ':' + digest(out / 'recorder.bin')
        saved = state / 'last-saved-key.txt'
        if saved.exists() and saved.read_text().strip() == key:
            return  # A frozen one-shot recorder must not produce endless copies.
        stamp = datetime.now(timezone.utc).strftime('%Y-%m-%d_%H%M%SZ')
        incident = root / 'incidents' / (stamp + '_charge-autosave')
        incident.mkdir(parents=True, exist_ok=False)
        out.rename(incident / 'mcu-capture')
        (incident / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
        (incident / 'observation.json').write_text(json.dumps(dict(utc=utc(), deployment=str(path),
             pi_boot_id=Path('/proc/sys/kernel/random/boot_id').read_text().strip(), snapshot=snapshot), indent=2)+'\n')
        info = subprocess.run(['docker', 'inspect', '--format',
              '{{.Name}} {{.Image}} {{.Config.Image}} {{index .Config.Labels "org.opencontainers.image.revision"}}',
              'mowgli-ros2', 'mowgli-gui', 'mowgli-gps'], capture_output=True, text=True, timeout=20)
        (incident / 'host-images.txt').write_text(info.stdout + info.stderr)
        (incident / 'README.md').write_text(
            '# Automatic charge evidence\n\nCaptured '+utc()+' from '+str(path)+'.\n'
            'See manifest.json for exact firmware/ELF/address and host-images.txt for running containers.\n'
            'The matching ELF remains in the deployment folder on this Pi.\n'
            'Read-only mem_ap capture; no halt/reset/SWO/retry or motor command.\n'
            'Reason 4 can freeze on a harmless transient; this is evidence, not proof of a trip.\n'
            'A frozen recorder stays one-shot until MCU reboot. Inspect before recovery.\n')
        saved.write_text(key+'\n')  # Mark saved only after evidence/provenance is durable.
        print('SAVED '+str(incident), flush=True)


def main():
    import fcntl
    import os
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path('/home/pi/mower-backups/192.168.1.118'))
    args = parser.parse_args()
    if os.getuid() == 0:
        raise RuntimeError('Run as pi, not root')
    root = args.root.resolve()
    state = root / 'diagnostics' / 'autosave'
    state.mkdir(parents=True, exist_ok=True)
    # Same lock path is used by the flash helper. Stop this service first too.
    with (state / 'openocd.lock').open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        report = retain_telemetry(state)
        (state / 'retention.json').write_text(json.dumps(dict(utc=utc(), **report), indent=2)+'\n')
        if report['removed']:
            print('Routine telemetry cleanup: '+json.dumps(report), flush=True)
        last_probe, last_log, idle_since = 0, 0, None
        while True:
            path, manifest = deployment(root)
            with subprocess.Popen(['docker', 'exec', '-i', '--user', '1000:1000',
                  'mowgli-ros2', 'bash', '-lc',
                  'source /opt/ros/lyrical/setup.bash && source /ros2_ws/install/setup.bash && python3 -u -'],
                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1) as child:
                selector = selectors.DefaultSelector()
                try:
                    child.stdin.write(OBSERVER)
                    child.stdin.close()
                    selector.register(child.stdout, selectors.EVENT_READ)
                    idle_since = None
                    while child.poll() is None:
                        if not selector.select(timeout=5):
                            idle_since = None
                            continue
                        line = child.stdout.readline()
                        if not line:
                            break
                        try:
                            snapshot = json.loads(line)
                        except json.JSONDecodeError:
                            continue
                        now = time.monotonic()
                        if now-last_log >= 5:
                            row = dict(utc=utc(), deployment=str(path), **snapshot)
                            (state / 'status.json').write_text(json.dumps(row, indent=2)+'\n')
                            append_telemetry(state, row)
                            last_log = now
                        if not eligible(snapshot, manifest):
                            idle_since = None
                            continue
                        idle_since = idle_since or now
                        if now-idle_since < 15 or now-last_probe < 60:
                            continue
                        if Path((root/'CURRENT-DEPLOYMENT.txt').read_text().strip()).resolve() != path:
                            break  # Reload provenance when a maintenance flash changes it.
                        last_probe = now
                        try:
                            capture(root, path, manifest, snapshot, state)
                        except Exception as error:
                            print('Capture failed: '+str(error), flush=True)
                finally:
                    selector.close()
                    # Closing docker exec's stream does not command or reset the MCU.
                    child.terminate()
                    try:
                        child.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
            print('ROS observer stopped; retrying in 10 seconds', flush=True)
            time.sleep(10)


if __name__ == '__main__':
    main()
