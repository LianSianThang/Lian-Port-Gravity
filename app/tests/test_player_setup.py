import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

APP = Path(__file__).resolve().parents[1]
(APP / 'out').mkdir(exist_ok=True)
sys.path[:0] = [str(APP), str(APP / 'scripts')]
import player_setup
from runtime_files import install_bundle, write_manifest, pe_imports
from package_player import forbidden


def make_game(root, title='CUSA01130'):
    game = root / 'game'
    for relative in player_setup.INPUTS:
        path = game / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(b'local test input')
    value = title.encode() + b'\0'
    metadata = struct.pack('<4sIIII', b'\0PSF', 0x101, 36, 48, 1)
    metadata += struct.pack('<HHIII', 0, 0x204, len(value), len(value), 0)
    metadata += b'TITLE_ID\0'.ljust(12, b'\0') + value
    (game / 'sce_sys/param.sfo').write_bytes(metadata)
    (game / 'arc').mkdir(exist_ok=True)
    (game / 'arc/test.arc').write_bytes(b'test')
    return game


class PlayerSetupTests(unittest.TestCase):
    def test_failure_keeps_previous_boot_and_game(self):
        with tempfile.TemporaryDirectory(dir=APP / 'out', prefix='setup-test-') as temp:
            root = Path(temp)
            game = make_game(root)
            before = player_setup.game_fingerprint(game)
            out = root / 'out'
            out.mkdir()
            (out / 'boot_gravity.bin').write_bytes(b'previous boot')
            with patch.object(player_setup, 'prepare_and_link', side_effect=ValueError('bad SELF')):
                with self.assertRaisesRegex(ValueError, 'bad SELF'):
                    player_setup.prepare_game(game, out)
            self.assertEqual((out / 'boot_gravity.bin').read_bytes(), b'previous boot')
            self.assertEqual(player_setup.game_fingerprint(game), before)
            self.assertEqual(list(out.iterdir()), [out / 'boot_gravity.bin'])

    def test_wrong_region_missing_modules_and_changed_inputs(self):
        with tempfile.TemporaryDirectory(dir=APP / 'out', prefix='setup-test-') as temp:
            root = Path(temp)
            game = make_game(root, 'CUSA01113')
            with self.assertRaisesRegex(ValueError, 'CUSA01130'):
                player_setup.game_fingerprint(game)
            game = make_game(root)
            out = root / 'out'
            out.mkdir()
            (out / 'boot_gravity.bin').write_bytes(b'prepared')
            (out / 'prepared_game.json').write_text(json.dumps(player_setup.game_fingerprint(game)))
            self.assertFalse(player_setup.needs_preparation(game, out))
            (game / 'eboot.bin').write_bytes(b'different executable')
            self.assertTrue(player_setup.needs_preparation(game, out))
            (game / 'sce_module/libc.prx').unlink()
            with self.assertRaisesRegex(ValueError, 'libc.prx'):
                player_setup.game_fingerprint(game)

    def test_corrupt_bundle_rejected_before_install(self):
        with tempfile.TemporaryDirectory(dir=APP / 'out', prefix='setup-test-') as temp:
            root = Path(temp)
            bundle = root / 'bundle'
            bundle.mkdir()
            for name in ('gravity-probe-local.exe', 'lianport-guest-start.exe', 'SDL3.dll'):
                (bundle / name).write_bytes(b'binary')
            write_manifest(bundle)
            (bundle / 'SDL3.dll').write_bytes(b'tampered')
            with self.assertRaisesRegex(ValueError, 'verification failed'):
                install_bundle(bundle, root / 'install')
            self.assertFalse((root / 'install').exists())

    def test_bundle_rejects_path_escape(self):
        with tempfile.TemporaryDirectory(dir=APP / 'out', prefix='setup-test-') as temp:
            root = Path(temp)
            manifest = {'format': 1, 'files': {name: {} for name in (
                '../outside.dll', 'gravity-probe-local.exe', 'lianport-guest-start.exe')}}
            (root / 'runtime_manifest.json').write_text(json.dumps(manifest))
            with self.assertRaisesRegex(ValueError, 'Invalid runtime bundle filename'):
                install_bundle(root, root / 'destination')

    def test_package_excludes_private_inputs(self):
        for path in ('game/CUSA01130/eboot.bin', 'app/user/save/data.bin',
                     'app/out/boot_gravity.bin', 'app/logs/run.log', 'reference/file.pkg'):
            self.assertTrue(forbidden(path), path)
        self.assertFalse(forbidden('app/prepare_gravity.py'))
        self.assertFalse(forbidden('runtime/src/runtime_ngs2.c'))

    def test_pe_dependency_reader(self):
        data = bytearray(1024)
        data[:2] = b'MZ'
        struct.pack_into('<I', data, 0x3c, 0x80)
        data[0x80:0x84] = b'PE\0\0'
        struct.pack_into('<H', data, 0x86, 1)
        struct.pack_into('<H', data, 0x94, 0xf0)
        struct.pack_into('<H', data, 0x98, 0x20b)
        struct.pack_into('<Q', data, 0x98 + 24, 0x140000000)
        struct.pack_into('<II', data, 0x98 + 112 + 8, 0x1000, 40)
        struct.pack_into('<IIII', data, 0x188 + 8, 512, 0x1000, 512, 512)
        struct.pack_into('<IIIII', data, 512, 0, 0, 0, 0x1050, 0)
        data[592:605] = b'KERNEL32.dll\0'
        with tempfile.TemporaryDirectory(dir=APP / 'out', prefix='setup-test-') as temp:
            path = Path(temp) / 'fixture.exe'
            path.write_bytes(data)
            self.assertEqual(pe_imports(path), ['KERNEL32.dll'])
            path.write_bytes(b'truncated')
            with self.assertRaises(ValueError):
                pe_imports(path)


if __name__ == '__main__':
    unittest.main()
