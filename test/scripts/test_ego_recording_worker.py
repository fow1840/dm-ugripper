#!/usr/bin/env python3
import importlib.util
import argparse
import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


WORKER_PATH = (
    Path(__file__).resolve().parents[2]
    / "standalone"
    / "UgripperRuntime"
    / "ego"
    / "ego_recording_worker.py"
)
SPEC = importlib.util.spec_from_file_location("ego_recording_worker", WORKER_PATH)
WORKER = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
SPEC.loader.exec_module(WORKER)


class FakeAdb:
    def __init__(self, roots):
        self.roots = roots

    def run(self, *args, **kwargs):
        if args[:2] == ("shell", "test"):
            return subprocess.CompletedProcess(args, 0 if args[-1] in self.roots else 1, "", "")
        if len(args) == 2 and args[0] == "shell" and str(args[1]).startswith("find "):
            return subprocess.CompletedProcess(args, 0, "\n".join(self.roots) + "\n", "")
        raise AssertionError(f"unexpected adb command: {args}")


class EgoRecordingWorkerTest(unittest.TestCase):
    def test_time_sync_accepts_first_attempt_below_threshold(self):
        first_set = {"returncode": 0, "elapsed_ms": 2, "stdout": "", "stderr": ""}
        with mock.patch.object(WORKER, "TIME_SYNC_MAX_OFFSET_MS", 15.0), mock.patch.object(
            WORKER, "TIME_SYNC_SET_AHEAD_MS", 20.0
        ), mock.patch.object(WORKER, "now_ms", side_effect=[1000, 1004, 1008]), mock.patch.object(
            WORKER, "set_remote_time_ms", return_value=first_set
        ) as set_time, mock.patch.object(
            WORKER, "measure_time_offsets", return_value=[{"offset_ms": 8.0, "rtt_ms": 4}]
        ):
            result = WORKER.sync_ego_time(object())

        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["attempt_count"], 1)
        self.assertEqual(result["final_offset_ms"], 8.0)
        self.assertEqual(result["set_ahead_ms"], 20.0)
        set_time.assert_called_once_with(mock.ANY, 1024)

    def test_time_sync_retries_with_same_set_ahead(self):
        set_result = {"returncode": 0, "elapsed_ms": 2, "stdout": "", "stderr": ""}
        with mock.patch.object(WORKER, "TIME_SYNC_MAX_OFFSET_MS", 15.0), mock.patch.object(
            WORKER, "TIME_SYNC_SET_AHEAD_MS", 20.0
        ), mock.patch.object(WORKER, "now_ms", side_effect=[1000, 1004, 1008, 1012]), mock.patch.object(
            WORKER, "set_remote_time_ms", return_value=set_result
        ) as set_time, mock.patch.object(
            WORKER,
            "measure_time_offsets",
            side_effect=[
                [{"offset_ms": 24.0, "rtt_ms": 5}],
                [{"offset_ms": 6.0, "rtt_ms": 4}],
            ],
        ):
            result = WORKER.sync_ego_time(object())

        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["attempt_count"], 2)
        self.assertEqual(result["final_offset_ms"], 6.0)
        self.assertEqual(set_time.call_args_list[0].args[1], 1024)
        self.assertEqual(set_time.call_args_list[1].args[1], 1028)
        self.assertEqual([item["set_ahead_ms"] for item in result["attempts"]], [20.0, 20.0])

    def test_time_sync_reports_second_attempt_outside_threshold(self):
        set_result = {"returncode": 0, "elapsed_ms": 2, "stdout": "", "stderr": ""}
        with mock.patch.object(WORKER, "TIME_SYNC_MAX_OFFSET_MS", 15.0), mock.patch.object(
            WORKER, "TIME_SYNC_MAX_CORRECTIONS", 1
        ), mock.patch.object(
            WORKER, "TIME_SYNC_SET_AHEAD_MS", 20.0
        ), mock.patch.object(WORKER, "now_ms", side_effect=[1000, 1004, 1008, 1012]), mock.patch.object(
            WORKER, "set_remote_time_ms", return_value=set_result
        ), mock.patch.object(
            WORKER,
            "measure_time_offsets",
            side_effect=[
                [{"offset_ms": 28.0, "rtt_ms": 5}],
                [{"offset_ms": 18.5, "rtt_ms": 4}],
            ],
        ):
            result = WORKER.sync_ego_time(object())

        self.assertEqual(result["status"], "error")
        self.assertEqual(result["attempt_count"], 2)
        self.assertEqual(result["final_offset_ms"], 18.5)
        self.assertIn("offset_ms=18.500", result["error"])

    def test_time_sync_allows_three_corrections_and_keeps_history(self):
        set_result = {"returncode": 0, "elapsed_ms": 2, "stdout": "", "stderr": ""}
        with mock.patch.object(WORKER, "TIME_SYNC_MAX_OFFSET_MS", 15.0), mock.patch.object(
            WORKER, "TIME_SYNC_MAX_CORRECTIONS", 3
        ), mock.patch.object(WORKER, "TIME_SYNC_SET_AHEAD_MS", 20.0), mock.patch.object(
            WORKER, "now_ms", side_effect=[1000, 1004, 1008, 1012, 1016, 1020, 1024]
        ), mock.patch.object(WORKER, "set_remote_time_ms", return_value=set_result) as set_time, mock.patch.object(
            WORKER,
            "measure_time_offsets",
            side_effect=[
                [{"offset_ms": 40.0, "rtt_ms": 5}],
                [{"offset_ms": 30.0, "rtt_ms": 4}],
                [{"offset_ms": 20.0, "rtt_ms": 4}],
                [{"offset_ms": 10.0, "rtt_ms": 4}],
            ],
        ):
            result = WORKER.sync_ego_time(object())

        self.assertEqual(result["status"], "ok")
        self.assertEqual(result["attempt_count"], 4)
        self.assertEqual(result["correction_count"], 3)
        self.assertEqual(result["first_offset_ms"], 40.0)
        self.assertEqual(result["final_offset_ms"], 10.0)
        self.assertEqual([item["offset_ms"] for item in result["attempts"]], [40.0, 30.0, 20.0, 10.0])
        self.assertEqual(set_time.call_count, 4)

    def test_time_sync_stops_after_three_corrections_when_still_outside_threshold(self):
        set_result = {"returncode": 0, "elapsed_ms": 2, "stdout": "", "stderr": ""}
        with mock.patch.object(WORKER, "TIME_SYNC_MAX_OFFSET_MS", 15.0), mock.patch.object(
            WORKER, "TIME_SYNC_MAX_CORRECTIONS", 3
        ), mock.patch.object(WORKER, "TIME_SYNC_SET_AHEAD_MS", 20.0), mock.patch.object(
            WORKER, "now_ms", side_effect=[1000, 1004, 1008, 1012, 1016, 1020, 1024]
        ), mock.patch.object(WORKER, "set_remote_time_ms", return_value=set_result) as set_time, mock.patch.object(
            WORKER,
            "measure_time_offsets",
            side_effect=[
                [{"offset_ms": 40.0, "rtt_ms": 5}],
                [{"offset_ms": 35.0, "rtt_ms": 4}],
                [{"offset_ms": 30.0, "rtt_ms": 4}],
                [{"offset_ms": 25.0, "rtt_ms": 4}],
            ],
        ):
            result = WORKER.sync_ego_time(object())

        self.assertEqual(result["status"], "error")
        self.assertEqual(result["attempt_count"], 4)
        self.assertEqual(result["correction_count"], 3)
        self.assertEqual(result["final_offset_ms"], 25.0)
        self.assertEqual(set_time.call_count, 4)

    def test_time_sync_requires_strictly_less_than_fifteen_ms(self):
        set_result = {"returncode": 0, "elapsed_ms": 2, "stdout": "", "stderr": ""}
        with mock.patch.object(WORKER, "TIME_SYNC_MAX_CORRECTIONS", 1), mock.patch.object(
            WORKER, "TIME_SYNC_SET_AHEAD_MS", 20.0
        ), mock.patch.object(
            WORKER, "now_ms", side_effect=[1000, 1004, 1008, 1012]
        ), mock.patch.object(WORKER, "set_remote_time_ms", return_value=set_result), mock.patch.object(
            WORKER,
            "measure_time_offsets",
            side_effect=[
                [{"offset_ms": 15.0, "rtt_ms": 4}],
                [{"offset_ms": -15.0, "rtt_ms": 4}],
            ],
        ):
            result = WORKER.sync_ego_time(object())

        self.assertEqual(result["status"], "error")
        self.assertEqual(result["attempt_count"], 2)
        self.assertEqual(result["final_offset_ms"], -15.0)

    def test_discovers_serial_scoped_dataset_root(self):
        root = "/sdcard/Android/data/com.ssnwt.helloxr/files/1150063703226061200012/data/dataset"
        with mock.patch.dict(WORKER.os.environ, {}, clear=True):
            self.assertEqual(WORKER.list_dataset_roots(FakeAdb([root])), [root])

    def test_explicit_root_override_wins(self):
        override = "/custom/ego/dataset"
        with mock.patch.dict(WORKER.os.environ, {WORKER.REMOTE_ROOT_OVERRIDE_ENV: override}, clear=True):
            self.assertEqual(WORKER.list_dataset_roots(FakeAdb([])), [override])

    def test_finds_only_new_temp_episode_and_keeps_its_root(self):
        legacy_root = "/sdcard/Android/data/com.ssnwt.helloxr/files/dataset"
        serial_root = "/sdcard/Android/data/com.ssnwt.helloxr/files/1150063703226061200012/data/dataset"
        with mock.patch.object(WORKER, "list_dataset_roots", return_value=[legacy_root, serial_root]), mock.patch.object(
            WORKER,
            "list_temp_episodes",
            side_effect=lambda _adb, root: {
                legacy_root: ["episode_20260720_0001-temp"],
                serial_root: ["episode_20260720_0018-temp"],
            }[root],
        ):
            candidates = WORKER.find_new_temp_episodes(
                object(), {legacy_root: {"episode_20260720_0001-temp"}, serial_root: set()}
            )
        self.assertEqual(candidates, [(serial_root, "episode_20260720_0018-temp")])

    def test_remote_path_is_scoped_to_discovered_root(self):
        root = "/sdcard/Android/data/com.ssnwt.helloxr/files/serial/data/dataset"
        self.assertEqual(
            WORKER.remote_path(root, "episode_20260720_0018-temp", "sensor.mcap"),
            root + "/episode_20260720_0018-temp/sensor.mcap",
        )

    def test_bind_persists_the_only_eligible_ego(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            binding_file = root / "binding.json"
            status_file = root / "status.json"
            args = argparse.Namespace(
                serial="",
                binding_file=binding_file,
                binding_status_file=status_file,
            )
            selected = mock.Mock(serial="ego-001")
            details = [{"serial": "ego-001", "props_match": True}]
            with mock.patch.object(WORKER, "detect_egos", return_value=([selected], details)):
                self.assertEqual(WORKER.run_bind(args), 0)

            binding = json.loads(binding_file.read_text(encoding="utf-8"))
            self.assertEqual(binding["serial"], "ego-001")
            status = json.loads(status_file.read_text(encoding="utf-8"))
            self.assertEqual(status["status"], "bound")

    def test_bind_rejects_multiple_eligible_egos(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            binding_file = root / "binding.json"
            args = argparse.Namespace(
                serial="",
                binding_file=binding_file,
                binding_status_file=root / "status.json",
            )
            devices = [mock.Mock(serial="ego-001"), mock.Mock(serial="ego-002")]
            with mock.patch.object(WORKER, "detect_egos", return_value=(devices, [])):
                self.assertEqual(WORKER.run_bind(args), 1)
            self.assertFalse(binding_file.exists())

    def test_probe_requires_bound_serial_in_adb_devices(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            binding_file = root / "binding.json"
            binding_file.write_text('{"serial":"ego-001"}\n', encoding="utf-8")
            args = argparse.Namespace(
                binding_file=binding_file,
                binding_status_file=root / "status.json",
            )
            with mock.patch.object(WORKER, "Adb", return_value=object()), mock.patch.object(
                WORKER, "list_devices", return_value=["ego-002"]
            ):
                self.assertEqual(WORKER.run_probe(args), 1)

            status = json.loads(args.binding_status_file.read_text(encoding="utf-8"))
            self.assertEqual(status["status"], "disconnected")

    def test_unbind_disconnected_ego_without_accessing_adb(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            binding_file = root / "binding.json"
            status_file = root / "status.json"
            binding_file.write_text('{"serial":"ego-001"}\n', encoding="utf-8")
            status_file.write_text(
                '{"serial":"ego-001","status":"disconnected"}\n', encoding="utf-8"
            )
            args = argparse.Namespace(
                binding_file=binding_file,
                binding_status_file=status_file,
            )

            with mock.patch.object(WORKER, "Adb", side_effect=AssertionError("ADB must not be used")):
                self.assertEqual(WORKER.run_unbind(args), 0)
            self.assertFalse(binding_file.exists())
            status = json.loads(status_file.read_text(encoding="utf-8"))
            self.assertEqual(status["status"], "unbound")
            self.assertNotIn("serial", status)

            self.assertEqual(WORKER.run_unbind(args), 0)
            self.assertEqual(
                json.loads(status_file.read_text(encoding="utf-8"))["status"],
                "unbound",
            )

    def test_stale_probe_cannot_restore_connected_status_after_unbind(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            binding_file = root / "binding.json"
            status_file = root / "status.json"
            binding_file.write_text('{"serial":"ego-001"}\n', encoding="utf-8")
            args = argparse.Namespace(
                binding_file=binding_file,
                binding_status_file=status_file,
            )

            def unbind_while_probe_is_running(_adb):
                self.assertEqual(WORKER.run_unbind(args), 0)
                return ["ego-001"]

            with mock.patch.object(WORKER, "Adb", return_value=object()), mock.patch.object(
                WORKER, "list_devices", side_effect=unbind_while_probe_is_running
            ):
                self.assertEqual(WORKER.run_probe(args), 1)

            status = json.loads(status_file.read_text(encoding="utf-8"))
            self.assertEqual(status["status"], "unbound")
            self.assertNotIn("serial", status)

    def test_unbind_restores_binding_when_status_publish_fails(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            binding_file = root / "binding.json"
            status_file = root / "status.json"
            binding_file.write_text('{"serial":"ego-001"}\n', encoding="utf-8")
            status_file.write_text(
                '{"serial":"ego-001","status":"connected"}\n', encoding="utf-8"
            )
            args = argparse.Namespace(
                binding_file=binding_file,
                binding_status_file=status_file,
            )

            with mock.patch.object(WORKER, "write_status", side_effect=OSError("write failed")):
                self.assertEqual(WORKER.run_unbind(args), 1)

            binding = json.loads(binding_file.read_text(encoding="utf-8"))
            self.assertEqual(binding["serial"], "ego-001")
            status = json.loads(status_file.read_text(encoding="utf-8"))
            self.assertEqual(status["status"], "connected")

    def test_stale_probe_cannot_overwrite_a_new_binding(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            binding_file = root / "binding.json"
            status_file = root / "status.json"
            binding_file.write_text('{"serial":"ego-001"}\n', encoding="utf-8")
            args = argparse.Namespace(
                binding_file=binding_file,
                binding_status_file=status_file,
            )

            def replace_binding_while_probe_is_running(_adb):
                with WORKER.binding_state_lock(binding_file):
                    WORKER.write_json_atomic(binding_file, {"serial": "ego-002"})
                    WORKER.write_status(
                        status_file,
                        {"status": "bound", "serial": "ego-002", "updated_at_ms": WORKER.now_ms()},
                    )
                return ["ego-001"]

            with mock.patch.object(WORKER, "Adb", return_value=object()), mock.patch.object(
                WORKER, "list_devices", side_effect=replace_binding_while_probe_is_running
            ):
                self.assertEqual(WORKER.run_probe(args), 1)

            status = json.loads(status_file.read_text(encoding="utf-8"))
            self.assertEqual(status["status"], "bound")
            self.assertEqual(status["serial"], "ego-002")

    def test_failed_stale_probe_cannot_overwrite_unbound_status(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            binding_file = root / "binding.json"
            status_file = root / "status.json"
            binding_file.write_text('{"serial":"ego-001"}\n', encoding="utf-8")
            args = argparse.Namespace(
                binding_file=binding_file,
                binding_status_file=status_file,
            )

            def unbind_then_fail(_adb):
                self.assertEqual(WORKER.run_unbind(args), 0)
                raise RuntimeError("adb devices failed")

            with mock.patch.object(WORKER, "Adb", return_value=object()), mock.patch.object(
                WORKER, "list_devices", side_effect=unbind_then_fail
            ):
                self.assertEqual(WORKER.run_probe(args), 1)

            status = json.loads(status_file.read_text(encoding="utf-8"))
            self.assertEqual(status["status"], "unbound")
            self.assertNotIn("serial", status)

    def test_start_uses_non_terminal_status_while_preparing_bound_ego(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            args = argparse.Namespace(
                serial="ego-001",
                episode_dir=root / "episode_0001-temp",
                status_file=root / "ego_sync.json",
                codec="h265",
                detect_timeout=1.0,
                interval=0.01,
            )
            adb = mock.Mock(serial="ego-001")
            adb.run.return_value = subprocess.CompletedProcess([], 0, "", "")
            snapshots = []

            with mock.patch.object(WORKER, "detect_ego", return_value=(adb, {})), mock.patch.object(
                WORKER, "sync_ego_time", return_value={"status": "ok"}
            ), mock.patch.object(WORKER, "snapshot_temp_episodes", return_value={}), mock.patch.object(
                WORKER, "set_ego_video_codec", return_value={"returncode": 0}
            ), mock.patch.object(
                WORKER,
                "find_new_temp_episodes",
                return_value=[("/remote/dataset", "episode_0001-temp")],
            ), mock.patch.object(
                WORKER, "sync_episode_once", side_effect=KeyboardInterrupt
            ), mock.patch.object(
                WORKER,
                "write_status",
                side_effect=lambda _path, payload: snapshots.append(dict(payload)),
            ):
                self.assertEqual(WORKER.run_start(args), 0)

            self.assertTrue(snapshots)
            self.assertEqual(snapshots[0]["status"], "starting")
            self.assertNotIn("not_found", [payload["status"] for payload in snapshots])

    def test_start_stops_before_broadcast_when_time_sync_fails(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            args = argparse.Namespace(
                serial="ego-001",
                episode_dir=root / "episode_0001-temp",
                status_file=root / "ego_sync.json",
                codec="h264",
                detect_timeout=1.0,
                interval=0.01,
            )
            adb = mock.Mock(serial="ego-001")
            failed_sync = {
                "status": "error",
                "threshold_ms": 15.0,
                "attempt_count": 2,
                "set_ahead_ms": 20.0,
                "first_offset_ms": 26.0,
                "final_offset_ms": 18.5,
                "error": "ego time sync failed offset_ms=18.500 threshold_ms=15",
            }
            with mock.patch.object(WORKER, "detect_ego", return_value=(adb, {})), mock.patch.object(
                WORKER, "sync_ego_time", return_value=failed_sync
            ), mock.patch.object(WORKER, "ugripper_log") as log:
                self.assertEqual(WORKER.run_start(args), 1)

            status = json.loads(args.status_file.read_text(encoding="utf-8"))
            self.assertEqual(status["status"], "time_sync_failed")
            self.assertEqual(status["error_type"], "ego_time_sync_failed")
            self.assertEqual(status["time_sync"]["final_offset_ms"], 18.5)
            adb.run.assert_not_called()
            log.assert_called_once_with(
                "ERROR",
                "ego time sync failed offset_ms=18.500 threshold_ms=15",
                component="ego",
                error_code="UGR-82-02",
            )


if __name__ == "__main__":
    unittest.main()
