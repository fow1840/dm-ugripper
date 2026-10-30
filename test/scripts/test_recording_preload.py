#!/usr/bin/env python3
import ctypes
import importlib.util
import mmap
from pathlib import Path
import resource
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('preload', Path(__file__).resolve().parents[2] / 'config/recording_preload.py')
p = importlib.util.module_from_spec(spec)
spec.loader.exec_module(p)


class PreloadTest(unittest.TestCase):
    def test_lock_shared_pages_deduplicate_and_release(self):
        if resource.getrlimit(resource.RLIMIT_MEMLOCK)[0] < mmap.PAGESIZE:
            self.skipTest('host does not allow one locked page')
        with tempfile.TemporaryDirectory() as d:
            path = Path(d) / 'tool'
            path.write_bytes(b'hello')
            alias = Path(d) / 'alias'
            alias.symlink_to(path)
            locked = p.LockedFiles()
            try:
                locked.acquire([path, alias])
                self.assertEqual(locked.bytes, mmap.PAGESIZE)
                self.assertEqual(len(locked.maps), 1)
                self.assertEqual(ctypes.string_at(*locked.maps[0]), b'hello')
                # File writes must remain visible: this is the file page, not COW.
                path.write_bytes(b'world')
                self.assertEqual(ctypes.string_at(*locked.maps[0]), b'world')
            finally:
                locked.close()
            self.assertEqual(locked.bytes, 0)
            self.assertEqual(locked.maps, [])
            self.assertEqual(path.read_bytes(), b'world')

    def test_budget_rejects_before_mapping(self):
        with tempfile.NamedTemporaryFile() as f:
            f.write(b'x'); f.flush()
            locked = p.LockedFiles()
            with patch.object(p, 'MAX_BYTES', 0), self.assertRaisesRegex(RuntimeError, 'budget'):
                locked.acquire([Path(f.name)])
            self.assertEqual(locked.maps, [])

    def test_low_memory_rejects(self):
        with tempfile.NamedTemporaryFile() as f:
            f.write(b'x'); f.flush()
            locked = p.LockedFiles()
            with patch.object(Path, 'read_text', return_value='MemAvailable: 1 kB\n'), self.assertRaisesRegex(RuntimeError, 'insufficient memory'):
                locked.acquire([Path(f.name)])
            self.assertEqual(locked.maps, [])

    def test_failure_releases_previous_mappings(self):
        with tempfile.TemporaryDirectory() as d:
            paths = [Path(d)/str(n) for n in range(2)]
            for path in paths: path.write_bytes(b'x')
            locked = p.LockedFiles()
            with patch.object(locked.libc, 'mlock', side_effect=[0, -1]), self.assertRaises(OSError):
                locked.acquire(paths)
            self.assertEqual(locked.maps, [])
            self.assertEqual(locked.bytes, 0)

    def test_missing_tool_fails(self):
        with patch.object(p.shutil, 'which', return_value=None), self.assertRaisesRegex(RuntimeError, 'not found'):
            p.discover_tools(p.time.monotonic()+10)

    def test_preparation_failure_does_not_launch_runtime(self):
        with patch.object(p.sys, 'argv', ['preload', '--', '/bin/true']), patch.object(p, 'discover_tools', side_effect=RuntimeError('test')), patch.object(p.subprocess, 'Popen') as child, patch.object(p.signal, 'signal'), patch.object(p.signal, 'alarm'):
            self.assertEqual(p.main(), 1)
            child.assert_not_called()


if __name__ == '__main__': unittest.main()
