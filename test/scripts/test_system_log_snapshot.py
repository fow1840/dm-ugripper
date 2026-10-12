#!/usr/bin/env python3
"""Finite snapshot, incremental append and disk-removal durability contracts."""
import json
import fcntl
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts/logging/save_system_log.sh"

JOURNAL = r'''#!/usr/bin/env python3
import json, os, sys, time
from pathlib import Path
args = sys.argv[1:]
with open(os.environ["CALLS"], "a") as f:
    f.write(json.dumps(args) + "\n")
time.sleep(float(os.environ.get("JOURNAL_DELAY", "0")))
if any(a == "--after-cursor=lost" for a in args):
    sys.exit(1)
cutoff = float(next(a.split("@", 1)[1] for a in args if a.startswith("--until=")))
after = next((a.split("=", 1)[1] for a in args if a.startswith("--after-cursor=")), "")
records = json.loads(Path(os.environ["RECORDS"]).read_text())
start = next((i + 1 for i, r in enumerate(records) if r[0] == after), 0)
selected = [r for r in records[start:] if r[1] <= cutoff]
for cursor, timestamp, line in selected:
    print(line)
if selected:
    print("-- cursor: " + selected[-1][0])
'''
SYNC = r'''#!/usr/bin/env python3
import os, sys, time
with open(os.environ["SYNCS"], "a") as f:
    f.write(sys.argv[-1] + "\n")
time.sleep(float(os.environ.get("SYNC_DELAY", "0")))
sys.exit(int(os.environ.get("SYNC_FAIL", "0")))
'''


class SystemLogSnapshotTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.bin = self.root / "bin"
        self.bin.mkdir()
        for name, source in (("journalctl", JOURNAL), ("sync", SYNC)):
            p = self.bin / name
            p.write_text(source.replace("#!/usr/bin/env python3", "#!" + sys.executable, 1))
            p.chmod(0o755)
        self.out = self.root / "disk logs"
        self.out.mkdir()
        self.env = dict(os.environ, PATH=str(self.bin) + ":" + os.environ["PATH"],
                        TMPDIR=str(self.root), RECORDS=str(self.root / "records"),
                        CALLS=str(self.root / "calls"), SYNCS=str(self.root / "syncs"))
        self.dest = self.out / "system_device_20261012.log"
        self.state = self.out / ".system_journal_cursor_device"
        self.records = [["a", 1, "first"], ["b", 2, "second"], ["c", 3, "future"]]
        self.write_records()

    def write_records(self):
        Path(self.env["RECORDS"]).write_text(json.dumps(self.records))

    def run_sync(self, cutoff="2", **env):
        return subprocess.run(["bash", str(SCRIPT), str(self.out), "DEVICE", "--sync",
                               cutoff, "20261012"], env=dict(self.env, **env),
                              capture_output=True, text=True, timeout=5)

    def test_snapshot_boundary_and_incremental_append_keep_inode(self):
        self.assertEqual(self.run_sync().returncode, 0)
        self.assertEqual(self.dest.read_text(), "first\nsecond\n")
        self.assertEqual(self.state.read_text(), "b\n")
        inode = self.dest.stat().st_ino
        self.assertEqual(self.run_sync("3").returncode, 0)
        self.assertEqual(self.dest.read_text(), "first\nsecond\nfuture\n")
        self.assertEqual(self.dest.stat().st_ino, inode)
        calls = [json.loads(s) for s in Path(self.env["CALLS"]).read_text().splitlines()]
        self.assertIn("--after-cursor=b", calls[-1])
        syncs = Path(self.env["SYNCS"]).read_text().splitlines()
        self.assertIn(str(self.dest), syncs)
        self.assertIn(str(self.out), syncs)

    def test_empty_snapshot_does_not_repeat_data(self):
        self.assertEqual(self.run_sync().returncode, 0)
        before = self.dest.read_bytes()
        self.assertEqual(self.run_sync().returncode, 0)
        self.assertEqual(self.dest.read_bytes(), before)
        self.assertEqual(self.state.read_text(), "b\n")

    def test_cap_keeps_recent_whole_lines_and_leaves_append_headroom(self):
        self.records = [[str(i), i, "record-" + str(i).zfill(4) + "-" + "x" * 30]
                        for i in range(40)]
        self.write_records()
        self.assertEqual(self.run_sync("40", UGRIPPER_SYSTEM_LOG_MAX_BYTES="1024").returncode, 0)
        self.assertLessEqual(self.dest.stat().st_size, 768)
        self.assertTrue(self.dest.read_text().startswith("record-"))
        self.assertTrue(self.dest.read_text().endswith(self.records[-1][2] + "\n"))
        inode = self.dest.stat().st_ino
        self.records.append(["40", 41, "next-record"])
        self.write_records()
        self.assertEqual(self.run_sync("41", UGRIPPER_SYSTEM_LOG_MAX_BYTES="1024").returncode, 0)
        self.assertEqual(self.dest.stat().st_ino, inode)

    def test_sync_failure_does_not_commit_cursor(self):
        self.state.write_text("a\n")
        result = self.run_sync(SYNC_FAIL="1")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.state.read_text(), "a\n")

    def test_cap_sync_failure_keeps_original_file_and_cursor(self):
        original = b"old-line\n" * 110
        self.dest.write_bytes(original)
        self.state.write_text("a\n")
        result = self.run_sync(SYNC_FAIL="1", UGRIPPER_SYSTEM_LOG_MAX_BYTES="256")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.dest.read_bytes(), original)
        self.assertEqual(self.state.read_text(), "a\n")

    def test_concurrent_snapshot_waits_for_previous_writer(self):
        lock = self.root / "ugripper-system-log-device.lock"
        with lock.open("w") as handle:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
            process = subprocess.Popen(["bash", str(SCRIPT), str(self.out), "DEVICE"],
                                       env=self.env, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            try:
                time.sleep(0.15)
                self.assertIsNone(process.poll())
                self.assertFalse(Path(self.env["CALLS"]).exists())
            finally:
                fcntl.flock(handle, fcntl.LOCK_UN)
                process.communicate(timeout=5)
        self.assertEqual(process.returncode, 0)
        self.assertEqual(self.state.read_text(), "c\n")

    def test_timeout_does_not_commit_partial_export_or_cursor(self):
        self.state.write_text("a\n")
        result = self.run_sync(JOURNAL_DELAY="1", UGRIPPER_SYSTEM_LOG_READ_TIMEOUT_S="0.1")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.state.read_text(), "a\n")
        self.assertFalse(self.dest.exists())
        self.assertEqual(len(Path(self.env["CALLS"]).read_text().splitlines()), 1)

    def test_lost_cursor_recovers_current_boot_with_same_boundary(self):
        self.state.write_text("lost\n")
        self.assertEqual(self.run_sync().returncode, 0)
        calls = [json.loads(s) for s in Path(self.env["CALLS"]).read_text().splitlines()]
        self.assertEqual(len(calls), 2)
        self.assertIn("-b", calls[-1])
        self.assertIn("--until=@2", calls[-1])
        self.assertEqual(self.state.read_text(), "b\n")

    def test_request_waits_for_slow_disk_sync_and_directory_commit(self):
        started = time.monotonic()
        result = subprocess.run(["bash", str(SCRIPT), str(self.out), "DEVICE"],
                                env=dict(self.env, SYNC_DELAY="0.15"),
                                capture_output=True, timeout=3)
        self.assertEqual(result.returncode, 0)
        self.assertGreaterEqual(time.monotonic() - started, 0.6)
        self.assertEqual(self.state.read_text(), "c\n")
        self.assertEqual(Path(self.env["SYNCS"]).read_text().splitlines()[-1], str(self.out))


if __name__ == "__main__":
    unittest.main()
