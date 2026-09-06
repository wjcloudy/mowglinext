#!/usr/bin/env python3
"""Read a frozen charge recorder through mem_ap, without CPU halt/reset or SWO."""
import argparse
import csv
import json
from pathlib import Path
import re
import struct
import subprocess
import time

HEADER = struct.Struct('<12I')
RAW = struct.Struct('<I6H')
CONTROL = struct.Struct('<IIHBB6fI')
SIZE = 21552
KEYS = ('magic', 'version', 'raw_capacity', 'control_capacity', 'raw_count',
        'control_count', 'freeze_reason', 'trigger_tick', 'missed_batches',
        'max_gap_ms', 'raw_seq', 'control_seq')


def header(blob):
    if len(blob) < HEADER.size:
        raise ValueError('Truncated header')
    h = dict(zip(KEYS, HEADER.unpack_from(blob)))
    if (h['magic'], h['version'], h['raw_capacity'], h['control_capacity']) != (
            0x43484447, 1, 1024, 128):
        raise ValueError('Wrong diagnostic firmware address or unsupported ABI')
    return h


def decode(blob):
    if len(blob) != SIZE:
        raise ValueError('Wrong dump length')
    h = header(blob)
    if h['freeze_reason'] not in (1, 2) or h['raw_seq'] & 1 or h['control_seq'] & 1:
        raise ValueError('Recorder is live or being written; capture after freeze')
    def rows(count, capacity, start, fmt, keys):
        return [dict(zip(keys, fmt.unpack_from(blob, start + (i % capacity) * fmt.size)))
                for i in range(max(0, count - capacity), count)]
    raw = rows(h['raw_count'], 1024, HEADER.size, RAW,
               ('batch_tick', 'adc_current', 'adc_output', 'adc_battery',
                'adc_input', 'adc_ntc', 'batch_row'))
    for r in raw:
        r['current_A_before_offset'] = (r['adc_current'] / 4095 * 3.3 - 2.5) * 100 / 12
    control = rows(h['control_count'], 128, HEADER.size + 1024 * RAW.size, CONTROL,
                   ('tick', 'gap_ms', 'pwm', 'state', 'adc_fault', 'battery', 'output',
                    'input', 'current', 'current_before_offset', 'temperature', 'missed_batches'))
    return {'header': h, 'raw': raw, 'control': control}


def save_decoded(blob, directory):
    data = decode(blob)
    (directory / 'decoded.json').write_text(json.dumps(data, indent=2) + '\n')
    for key in ('raw', 'control'):
        with (directory / (key + '.csv')).open('w', newline='') as f:
            if data[key]:
                writer = csv.DictWriter(f, fieldnames=data[key][0])
                writer.writeheader()
                writer.writerows(data[key])
    print(json.dumps(data['header'], indent=2))


def capture(address, directory, watch):
    if address % 4 or not (0x20000000 <= address <= 0x2000C000 - SIZE):
        raise ValueError('Recorder must fit the STM32F401VC SRAM range')
    directory = directory.resolve()
    if not re.fullmatch(r'[A-Za-z0-9_./-]+', str(directory)):
        raise ValueError('Use a simple absolute Linux path without spaces for OpenOCD')
    directory.mkdir(parents=True, exist_ok=False)
    base = '''source [find interface/stlink.cfg]
transport select swd
adapter speed 100
reset_config none
source [find target/swj-dp.tcl]
swj_newdap mower cpu -irlen 4 -ircapture 0x1 -irmask 0xf -expected-id 0x2ba01477
dap create mower.dap -chain-position mower.cpu
target create mower.mem mem_ap -dap mower.dap -ap-num 0
gdb port disabled
tcl port disabled
telnet port disabled
init
'''
    def read_regions(regions):
        cfg = base + ''.join(f'dump_image {directory / name} {address:#x} {size}\n'
                             for name, size in regions) + 'shutdown\n'
        path = directory / 'read.cfg'
        path.write_text(cfg)
        result = subprocess.run(['sudo', '-n', 'openocd', '-f', str(path)],
                                capture_output=True, text=True, timeout=45)
        with (directory / 'openocd.log').open('a') as f:
            f.write(result.stdout + result.stderr)
        result.check_returncode()

    while True:
        read_regions([('header.bin', HEADER.size)])
        h = header((directory / 'header.bin').read_bytes())
        print(json.dumps(h), flush=True)
        if h['freeze_reason'] and not ((h['raw_seq'] | h['control_seq']) & 1):
            read_regions([('before.bin', HEADER.size), ('recorder.bin', SIZE), ('after.bin', HEADER.size)])
            blob = (directory / 'recorder.bin').read_bytes()
            if not ((directory / 'before.bin').read_bytes() == blob[:HEADER.size]
                    == (directory / 'after.bin').read_bytes()):
                raise ValueError('Recorder changed during capture; do not trust this dump')
            save_decoded(blob, directory)
            return
        if not watch:
            print('Recorder still live. No coherent full dump taken. Use --watch to wait for a fault.')
            return
        time.sleep(5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest='command', required=True)
    dec = sub.add_parser('decode')
    dec.add_argument('binary', type=Path)
    cap = sub.add_parser('capture')
    cap.add_argument('--address', required=True, type=lambda s: int(s, 0))
    cap.add_argument('--directory', required=True, type=Path)
    cap.add_argument('--watch', action='store_true')
    args = parser.parse_args()
    if args.command == 'decode':
        save_decoded(args.binary.read_bytes(), args.binary.parent)
    else:
        capture(args.address, args.directory, args.watch)


if __name__ == '__main__':
    main()
