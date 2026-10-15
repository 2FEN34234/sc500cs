import pathlib
import shutil
import subprocess
import sys
import tempfile
import unittest


class ConfigTests(unittest.TestCase):
    def test_dns_identity_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as directory:
            script = pathlib.Path(directory) / 'init_config.py'
            shutil.copyfile(pathlib.Path(__file__).parents[1] / 'init_config.py', script)
            def run(*args):
                return subprocess.run([sys.executable, str(script), *args], capture_output=True)
            for host in ('101.43.33.186', 'https://camera.example.org', 'camera.example.invalid',
                         'x\nSECRET=oops', 'camera.example.org:443', '-bad.example.org'):
                self.assertNotEqual(run('--host', host).returncode, 0)
                self.assertFalse(script.with_name('.env').exists())
            self.assertNotEqual(run().returncode, 0)
            result = run('--host', 'Camera.Example.org')
            self.assertEqual(result.returncode, 0, result.stderr)
            original = script.with_name('.env').read_bytes()
            self.assertIn(b'SITE_ADDRESS=camera.example.org\n', original)
            values = dict(line.split('=', 1) for line in original.decode().splitlines())
            self.assertEqual(len(values['CAMERA_DEVICE_TOKEN']), 64)
            self.assertNotIn(values['CAMERA_DEVICE_TOKEN'].encode(), result.stdout)
            self.assertNotEqual(run('--host', 'new.example.org').returncode, 0)
            self.assertEqual(script.with_name('.env').read_bytes(), original)
