"""Guard tests for the separate Pi evidence saver; no hardware/debug access."""
import copy
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


if __name__ == '__main__':
    unittest.main()
