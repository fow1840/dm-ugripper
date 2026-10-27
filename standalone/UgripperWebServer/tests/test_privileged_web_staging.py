import os
from pathlib import Path
import subprocess
import tempfile
import unittest


class PrivilegedWebStagingTest(unittest.TestCase):
    def test_inspection_worker_dependencies_are_packaged_privately(self):
        script = (Path(__file__).resolve().parents[3]/'build_deb.sh').read_text()
        for name in ('inspection', 'encoder_probe', 'production_test', 'common', 'environment_config'):
            self.assertIn('copy_if_exists "standalone/UgripperWebServer/'+name+'.py" "$WEB_PRIVILEGED_ROOT/'+name+'.py"', script)

    def test_existing_staging_is_refreshed_without_nested_or_stale_modules(self):
        script = (Path(__file__).resolve().parents[3]/'build_deb.sh').read_text()
        start = script.index('# Refresh privileged Web directories in place;')
        end = script.index('copy_if_exists "$TARGET_INSTALL_ROOT/.venv', start)
        block = script[start:end]
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            target = root/'stage'
            for name in ('production', 'bin'):
                source = root/'standalone/UgripperWebServer'/name
                source.mkdir(parents=True)
                (source/'current').write_text('new')
                (target/name/name).mkdir(parents=True)
                (target/name/'current').write_text('old')
                os.utime(target/name/'current', (1, 1))
                (target/name/'obsolete').write_text('stale')
            env = dict(os.environ, WEB_PRIVILEGED_ROOT=str(target))
            subprocess.run(['/bin/bash', '-eu', '-c', block], cwd=root, env=env, check=True)
            for name in ('production', 'bin'):
                self.assertEqual((target/name/'current').read_text(), 'new')
                self.assertFalse((target/name/name).exists())
                self.assertFalse((target/name/'obsolete').exists())
                (root/'standalone/UgripperWebServer'/name/'current').write_text('newer')
            subprocess.run(['/bin/bash', '-eu', '-c', block], cwd=root, env=env, check=True)
            for name in ('production', 'bin'):
                self.assertEqual((target/name/'current').read_text(), 'newer')
