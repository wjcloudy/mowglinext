"""Guard tests for the separate Pi evidence saver; no hardware/debug access."""
import copy
from datetime import date, datetime, timezone
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
import charge_diag_dump as decoder
import charge_diag_autosave as saver


class AutosaveGuards(unittest.TestCase):
    def setUp(self):
        self.manifest = dict(version='1.12.179', protocol=8)
        self.snapshot = dict(age={k:0 for k in ('status','power','mission','wheel')}, values=dict(
            status=dict(fw='1.12.179', protocol=8, compatible=True, mow=False, rpm=0),
            power=dict(current=-0.4, output=0, battery=26),
            mission=dict(state='IDLE'), wheel=dict(speed=0, yaw_rate=0)))

    def test_failed_output_still_eligible(self):
        # A collapsed regulated rail must not exclude the fault we want to save.
        self.assertTrue(saver.eligible(self.snapshot, self.manifest))

    def test_each_stale_stream_blocks_reads(self):
        for key in self.snapshot['age']:
            s = copy.deepcopy(self.snapshot)
            s['age'][key] = 4
            self.assertFalse(saver.eligible(s, self.manifest), key)

    def test_movement_blade_wrong_identity_and_mission_block_reads(self):
        for key, field, value in [('status','fw','1.12.178'), ('status','protocol',7),
             ('status','compatible',False), ('status','mow',True), ('status','rpm',1),
             ('wheel','speed',0.01), ('wheel','yaw_rate',float('nan')),
             ('mission','state','AUTONOMOUS')]:
            s = copy.deepcopy(self.snapshot)
            s['values'][key][field] = value
            self.assertFalse(saver.eligible(s, self.manifest), (key,field))

    def test_deployment_rejects_wrong_elf_and_external_pointer(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            d = root/'deployments'/'fixture'
            d.mkdir(parents=True)
            (root/'CURRENT-DEPLOYMENT.txt').write_text(str(d))
            (d/'FLASH-VERIFIED').write_text('verified')
            for name in ('firmware.bin', 'firmware.elf'):
                (d/name).write_bytes(b'fixture')
            h = hashlib.sha256(b'fixture').hexdigest()
            m = dict(self.manifest, unit='192.168.1.118', target='Yardforce500B_LFP_DMA_DIAG',
                     charge_diag_abi=2, charge_diag_size=29688,
                     charge_diag_address='0x20000018', binary_sha256=h, elf_sha256=h)
            (d/'manifest.json').write_text(json.dumps(m))
            self.assertEqual(saver.deployment(root)[0], d.resolve())
            (d/'firmware.elf').write_bytes(b'wrong ELF')
            with self.assertRaisesRegex(ValueError, 'checksum'):
                saver.deployment(root)
            (root/'CURRENT-DEPLOYMENT.txt').write_text(str(root))
            with self.assertRaisesRegex(ValueError, 'verified deployment'):
                saver.deployment(root)

    def test_frozen_evidence_saved_once_across_restart(self):
        with tempfile.TemporaryDirectory() as temp:
            root, d = Path(temp), Path(temp)/'deployment'
            state = root/'state'
            state.mkdir()
            blob = bytearray(decoder.SIZE)
            decoder.HEADER.pack_into(blob, 0, 0x43484447, 2, 1024, 128, 0, 0, 2, 100, 0, 11, 0, 0)
            import struct
            struct.pack_into('<8I', blob, decoder.SIZES[1], 64, 32, 0, 0, 0, 0, 0, 0)
            manifest = dict(self.manifest, binary_sha256='fixture', charge_diag_address='0x20000018')
            def run(command, **kwargs):
                if command[0] == 'python3':
                    out = Path(command[-1])
                    out.mkdir()
                    (out/'header.bin').write_bytes(blob[:decoder.HEADER.size])
                    (out/'recorder.bin').write_bytes(blob)
                    decoder.save_decoded(blob, out)
                return saver.subprocess.CompletedProcess(command, 0, stdout='', stderr='')
            with patch.object(saver.subprocess, 'run', side_effect=run), \
                 patch.object(saver.Path, 'read_text', autospec=True) as read:
                # Keep all real file reads except the Linux-only boot ID.
                original = Path.open
                def text(path, *args, **kwargs):
                    if path.as_posix() == '/proc/sys/kernel/random/boot_id': return 'test-boot'
                    with original(path) as f: return f.read()
                read.side_effect = text
                saver.capture(root, d, manifest, self.snapshot, state)
                saver.capture(root, d, manifest, self.snapshot, state)
            incidents = list((root/'incidents').iterdir())
            self.assertEqual(len(incidents), 1)
            self.assertEqual((incidents[0]/'mcu-capture'/'recorder.bin').read_bytes(), blob)
            self.assertTrue((incidents[0]/'observation.json').exists())

    def test_retention_age_size_and_protected_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            state = root/'diagnostics/autosave'
            state.mkdir(parents=True)
            protected = [state/'recorder.bin', state/'latest-header.json',
                         state/'2026-99-99_telemetry.jsonl',
                         root/'incidents/old/mcu-capture/recorder.bin',
                         root/'deployments/old/firmware.bin']
            for path in protected:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b'KEEP evidence exactly')
            before = {p:p.read_bytes() for p in protected}
            # A directory whose name resembles a log must never be traversed.
            nested = state/'2026-10-01_telemetry.jsonl'
            nested.mkdir()
            (nested/'firmware.bin').write_bytes(b'KEEP nested')
            for name, size in [('2026-10-03_telemetry.jsonl',1),
                               ('2026-10-04_telemetry.jsonl',40),
                               ('2026-10-09_telemetry.jsonl',40),
                               ('2026-10-10_telemetry.jsonl',40)]:
                (state/name).write_bytes(b'x'*size)
            report = saver.retain_telemetry(state, date(2026,10,10), max_bytes=100)
            self.assertEqual(report['removed'], ['2026-10-03_telemetry.jsonl',
                                                 '2026-10-04_telemetry.jsonl'])
            self.assertEqual(report['retained_bytes'],80)
            self.assertEqual({p:p.read_bytes() for p in protected}, before)
            self.assertEqual((nested/'firmware.bin').read_bytes(), b'KEEP nested')

    def test_retention_seven_day_boundary_and_chunk_order(self):
        with tempfile.TemporaryDirectory() as temp:
            state = Path(temp)
            for name in ('2026-10-04_telemetry.jsonl',
                         '2026-10-10_telemetry.jsonl',
                         '2026-10-10_telemetry-0001.jsonl'):
                (state/name).write_bytes(b'x'*40)
            report = saver.retain_telemetry(state, date(2026,10,10), max_bytes=120)
            self.assertEqual(report['removed'], [])  # Oldest retained day, inclusive.
            report = saver.retain_telemetry(state, date(2026,10,10), max_bytes=40)
            self.assertEqual(report['removed'], ['2026-10-04_telemetry.jsonl',
                                                 '2026-10-10_telemetry.jsonl'])
            self.assertTrue((state/'2026-10-10_telemetry-0001.jsonl').exists())

    def test_symlinks_are_not_followed_or_deleted(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            state = root/'state'
            state.mkdir()
            backup = root/'firmware.bin'
            backup.write_bytes(b'KEEP backup')
            link = state/'2026-10-01_telemetry.jsonl'
            try:
                link.symlink_to(backup)
            except OSError:
                self.skipTest('Host cannot create symlinks')
            self.assertEqual(saver.retain_telemetry(state, date(2026,10,10),0)['removed'], [])
            self.assertTrue(link.is_symlink())
            self.assertEqual(backup.read_bytes(), b'KEEP backup')
            redirected = root/'redirected'
            redirected.symlink_to(state, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                saver.retain_telemetry(redirected)

    def test_rotation_keeps_previous_chunk(self):
        with tempfile.TemporaryDirectory() as temp:
            state = Path(temp)
            day = datetime.now(timezone.utc).date()
            original = state/f'{day}_telemetry.jsonl'
            original.write_bytes(b'old\n')
            with patch.object(saver, 'TELEMETRY_CHUNK_BYTES', 4):
                saver.append_telemetry(state, dict(test='new'))
            self.assertEqual(original.read_bytes(), b'old\n')
            self.assertEqual(json.loads((state/f'{day}_telemetry-0001.jsonl').read_text()), dict(test='new'))


if __name__ == '__main__':
    unittest.main()
