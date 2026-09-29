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


class EgoPackageTests(unittest.TestCase):
    def test_unrelated_android_device_does_not_abort_discovery(self):
        adb = mock.Mock(serial="phone")
        with mock.patch.object(WORKER, "get_props", return_value={}), \
             mock.patch.object(WORKER, "select_app") as select:
            self.assertFalse(WORKER.is_ego_device(adb)[0])
            select.assert_not_called()
        with mock.patch.object(WORKER, "get_props", return_value=WORKER.SXR_PROPS), \
             mock.patch.object(WORKER, "select_app", side_effect=RuntimeError("missing app")):
            self.assertFalse(WORKER.is_ego_device(adb)[0])

    def test_new_and_legacy_package_with_same_version(self):
        for package in WORKER.SUPPORTED_PACKAGES:
            adb = mock.Mock(serial="test-serial")
            adb.run.return_value = subprocess.CompletedProcess([], 0, f"package:{package}\n", "")
            self.assertEqual(WORKER.select_app(adb), package)
            adb.run.return_value = subprocess.CompletedProcess([], 0, "versionName=1.0.4\n", "")
            self.assertEqual(WORKER.read_app_version(adb)["package"], package)
            adb.run.return_value = subprocess.CompletedProcess([], 0, "", "")
            WORKER.list_dataset_roots(adb)
            command = adb.run.call_args.args[1]
            self.assertIn(f"/Android/data/{package}/files", command)
            other = "unsupported.package"
            self.assertNotIn(f"/Android/data/{other}/files", command)

    def test_unsupported_package_rejected(self):
        adb = mock.Mock()
        adb.run.return_value = subprocess.CompletedProcess([], 0,
            "package:com.ssnwt.egoserver\n", "")
        with self.assertRaisesRegex(RuntimeError, "no supported Ego"):
            WORKER.select_app(adb)

    def test_new_package_keeps_legacy_action_and_class(self):
        adb = mock.Mock(package="com.ssnwt.helloxr")
        adb.run.return_value = subprocess.CompletedProcess([], 0,
            'Broadcast completed: result=-1, data="{}"', '')
        WORKER.app_broadcast(adb, "SpeechCommandReceiver", "SPEECH_STATUS")
        args = adb.run.call_args.args
        self.assertIn("com.ssnwt.helloxr/com.ssnwt.helloxr.SpeechCommandReceiver", args)
        self.assertIn("com.ssnwt.helloxr.SPEECH_STATUS", args)


class EgoPromptLanguageTests(unittest.TestCase):
    def test_apply_and_verify_both_packages_and_languages(self):
        for package in WORKER.SUPPORTED_PACKAGES:
            for language in ('zh', 'en'):
                adb = mock.Mock()
                adb.run.side_effect = [
                    subprocess.CompletedProcess([], 0, 'Broadcast completed: result=0', ''),
                    subprocess.CompletedProcess([], 0, f'<map><string name="prompt_language">{language}</string></map>', '')]
                with mock.patch.object(WORKER, 'select_app', return_value=package):
                    self.assertEqual(WORKER.sync_prompt_language(adb, language)['status'], 'ok')
                self.assertIn(package, adb.run.call_args_list[0].args)
                self.assertIn(language, adb.run.call_args_list[0].args)

    def test_readback_failure_is_not_success(self):
        for result in [subprocess.CompletedProcess([], 1, '', 'not debuggable'),
                       subprocess.CompletedProcess([], 0, '<map><string name="prompt_language">zh</string></map>', '')]:
            adb = mock.Mock()
            adb.run.side_effect = [subprocess.CompletedProcess([], 0, 'Broadcast completed:', ''), result]
            with mock.patch.object(WORKER, 'select_app', return_value=WORKER.PACKAGE):
                self.assertEqual(WORKER.sync_prompt_language(adb, 'en')['status'], 'failed')


class EgoAntiFlickerTests(unittest.TestCase):
    def test_mapping_and_readback(self):
        for frequency, mode in [('50', '1'), ('60', '2')]:
            adb = mock.Mock()
            adb.run.side_effect = [subprocess.CompletedProcess([], 0, '0', ''),
                                   subprocess.CompletedProcess([], 0, '', ''),
                                   subprocess.CompletedProcess([], 0, mode, '')]
            self.assertEqual(WORKER.sync_power_line_frequency(adb, frequency)['status'], 'ok')
            self.assertEqual(adb.run.call_args_list[1].args,
                             ('shell', 'setprop', 'persist.sys.static.exp.enable', mode))

    def test_matching_mode_is_not_rewritten(self):
        adb = mock.Mock()
        adb.run.return_value = subprocess.CompletedProcess([], 0, '1', '')
        self.assertEqual(WORKER.sync_power_line_frequency(adb, '50')['status'], 'ok')
        self.assertEqual(adb.run.call_count, 1)

    def test_failed_or_mismatched_readback(self):
        for code in [0, 1]:
            adb = mock.Mock()
            adb.run.return_value = subprocess.CompletedProcess([], code, '0', '')
            self.assertEqual(WORKER.sync_power_line_frequency(adb, '60')['status'], 'failed')


class EgoSerialSyncTests(unittest.TestCase):
    def test_status_gate_and_forward_cleanup(self):
        for state, available, offset, rtt, expected in [
                ("synced", True, .2, .3, "ok"),
                ("measured_only", True, .2, .3, "error"),
                ("synced", False, .2, .3, "error"),
                ("synced", True, 6, .3, "error"),
                ("synced", True, float("nan"), .3, "error"),
                ("synced", True, .2, 20, "error")]:
            adb = mock.Mock(serial="ego", package="com.ssnwt.helloxr")
            adb.run.return_value = subprocess.CompletedProcess([], 0, "18099\n", "")
            payload = {"time_sync": dict(status=state, serial_available=available,
                                         offset_ms=offset, rtt_ms=rtt)}
            with mock.patch.object(WORKER.subprocess, "run", return_value=
                    subprocess.CompletedProcess([], 0, json.dumps(payload), "")), \
                 mock.patch.object(WORKER, "set_remote_time_ms") as set_time:
                self.assertEqual(WORKER.read_serial_time_sync(adb)["status"], expected)
                set_time.assert_not_called()
            self.assertEqual(adb.run.call_args.args, ("forward", "--remove", "tcp:18099"))

    def test_http_failure_releases_only_owned_forward(self):
        adb = mock.Mock(serial="ego", package="com.ssnwt.helloxr")
        adb.run.return_value = subprocess.CompletedProcess([], 0, "18099\n", "")
        with mock.patch.object(WORKER.subprocess, "run", side_effect=subprocess.TimeoutExpired("curl", 3)):
            self.assertEqual(WORKER.read_serial_time_sync(adb)["status"], "error")
        self.assertEqual(adb.run.call_args.args, ("forward", "--remove", "tcp:18099"))


class BatteryTests(unittest.TestCase):
    def test_battery_values_and_invalid_responses(self):
        adb = mock.Mock()
        for level, scale, present, valid in [(50, 200, 'true', True), (0, 100, 'true', True),
                                           (101, 100, 'true', False), (10, 0, 'true', False),
                                           (10, 100, 'false', False)]:
            adb.run.return_value = subprocess.CompletedProcess([], 0,
                f"  present: {present}\n  level: {level}\n  scale: {scale}\n  status: 2\n", '')
            result = WORKER.read_battery(adb)
            self.assertEqual(bool(result), valid)
            if valid:
                self.assertEqual(result['percent'], round(level * 100 / scale))
                self.assertTrue(result['charging'])
        adb.run.return_value = subprocess.CompletedProcess([], 1, '', 'offline')
        self.assertEqual(WORKER.read_battery(adb), {})


class FakeAdb:
    def __init__(self, roots, serial=""):
        self.roots = roots
        self.serial = serial
        self.commands = []

    def run(self, *args, **kwargs):
        self.commands.append(args)
        if args[:2] == ("shell", "test"):
            return subprocess.CompletedProcess(args, 0 if args[-1] in self.roots else 1, "", "")
        if len(args) == 2 and args[0] == "shell" and "find " in str(args[1]):
            return subprocess.CompletedProcess(args, 0, "\n".join(self.roots) + "\n", "")
        raise AssertionError(f"unexpected adb command: {args}")


class EgoAppFeaturesTest(unittest.TestCase):
    def test_app_version_gate(self):
        for version, expected in (("1.0.3", False), ("1.0.4", True), ("1.0.10", True),
                                  ("1.1.0", True), ("2.0.0", True), ("v1.0.4", True),
                                  ("", False), ("1.0.4-beta", False), ("garbage", False)):
            with self.subTest(version=version):
                adb = mock.Mock()
                adb.run.return_value = subprocess.CompletedProcess([], 0, f"  versionName={version}\n", "")
                self.assertEqual(WORKER.read_app_version(adb)["features_supported"], expected)
        adb.run.side_effect = subprocess.TimeoutExpired("adb", 5)
        self.assertFalse(WORKER.read_app_version(adb)["features_supported"])

    def test_current_limits_and_environment_parsing(self):
        with tempfile.TemporaryDirectory() as directory:
            env_file = Path(directory) / "environment"
            with mock.patch.object(WORKER, "ENVIRONMENT_FILE", env_file):
                for value, expected in ((None, 1100000), ("garbage", 1100000), ("2.5", 1100000),
                                        ("2.4.9", 1100000), ("2.5.0", 1100000), ("2.5.1", 1600000),
                                        ("2.5.10", 1600000), ("v2.5.1", 1600000), ("2.6.0", 1100000)):
                    with self.subTest(value=value):
                        if value is None:
                            env_file.unlink(missing_ok=True)
                        else:
                            env_file.write_text(f'export HARDWARE_VERSION="{value}" # board\n')
                        response = {"status": "ok", "requested_ua": expected, "callback": "success",
                                    "readback": str(expected), "matched": True, "persisted": True, "error": None}
                        adb = mock.Mock()
                        adb.run.return_value = subprocess.CompletedProcess(
                            [], 0, 'Broadcast completed: result=-1, data="' + json.dumps(response) + '"\n', "")
                        result = WORKER.configure_input_current(adb, {"features_supported": True})
                        self.assertEqual(result["status"], "ok")
                        self.assertEqual(result["current_ua"], expected)
                        self.assertEqual(adb.run.call_args.args[-3:], ("--ei", "current_ua", str(expected)))
                adb.reset_mock()
                self.assertEqual(WORKER.configure_input_current(adb, {"features_supported": False})["status"], "skipped")
                adb.run.assert_not_called()

    def test_current_rejects_unconfirmed_results(self):
        for output in ('Broadcast completed: result=0', 'Broadcast completed: result=-1, data="bad json"',
                       'Broadcast completed: result=-1, data="{}"',
                       'Broadcast completed: result=-1, data="{"status":"ok","persisted":false}"'):
            with self.subTest(output=output), mock.patch.object(WORKER, "read_environment_value", return_value="2.5.0"):
                adb = mock.Mock()
                adb.run.return_value = subprocess.CompletedProcess([], 0, output, "")
                self.assertEqual(WORKER.configure_input_current(adb, {"features_supported": True})["status"], "failed")

    def test_speech_selects_device_and_gates_broadcast(self):
        for bound in ("", "ego-001"):
            for supported, count, language in ((True, 1, "zh"), (True, 1, "en"), (False, 1, "zh"),
                                                (True, 0, "zh"), (True, 2, "zh")):
                with self.subTest(bound=bound, supported=supported, count=count, language=language):
                    args = argparse.Namespace(binding_file=Path("unused"), serial=bound)
                    adb = mock.Mock(serial="ego-001")
                    response = {"protocol": "speech.v1", "code": 1203, "language": language,
                                "status": "accepted", "error": None}
                    with mock.patch.object(WORKER, "read_status", return_value={"serial": bound}), \
                         mock.patch.object(WORKER, "detect_egos", return_value=([adb] * count, [])) as detect, \
                         mock.patch.object(WORKER, "read_app_version", return_value={"features_supported": supported}), \
                         mock.patch.object(WORKER, "read_environment_value", return_value=language), \
                         mock.patch.object(WORKER, "app_broadcast", return_value={"status": "returned", "response": response}) as broadcast, \
                         mock.patch.object(WORKER, "ugripper_log"):
                        self.assertEqual(WORKER.run_speech(args), 0)
                        if bound:
                            detect.assert_called_once_with(bound)
                        else:
                            detect.assert_not_called()
                        if bound and supported and count == 1:
                            broadcast.assert_called_once_with(adb, "SpeechCommandReceiver", "SPEECH_PLAY",
                                                              "--ei", "code", "1203", "--es", "language", language)
                        else:
                            broadcast.assert_not_called()

    def test_speech_skips_changed_binding_and_reports_rejection(self):
        args = argparse.Namespace(binding_file=Path("unused"), serial="ego-001")
        for changed in (True, False):
            with mock.patch.object(WORKER, "read_status", side_effect=[{"serial": "ego-001"}, {"serial": "" if changed else "ego-001"}]), \
                 mock.patch.object(WORKER, "detect_egos", return_value=([mock.Mock(serial="ego-001")], [])), \
                 mock.patch.object(WORKER, "read_app_version", return_value={"features_supported": True}), \
                 mock.patch.object(WORKER, "read_environment_value", return_value="zh"), \
                 mock.patch.object(WORKER, "app_broadcast", return_value={"status": "returned", "response": {"status": "rejected"}}) as broadcast, \
                 mock.patch.object(WORKER, "ugripper_log"):
                self.assertEqual(WORKER.run_speech(args), 0 if changed else 1)
                self.assertEqual(broadcast.call_count, 0 if changed else 1)

    def test_all_umi_codes_and_languages_use_confirmed_response(self):
        for code in (1302, 1003, 1203, 1204, 1205):
            for language in ('zh', 'en'):
                with self.subTest(code=code, language=language):
                    args = argparse.Namespace(binding_file=Path('unused'), serial='ego-001', speech_code=code)
                    response = {'protocol': 'speech.v1', 'code': code, 'language': language,
                                'status': 'accepted', 'error': None}
                    adb = mock.Mock(serial='ego-001')
                    with mock.patch.object(WORKER, 'read_status', return_value={'serial': 'ego-001'}), \
                         mock.patch.object(WORKER, 'detect_egos', return_value=([adb], [])), \
                         mock.patch.object(WORKER, 'read_app_version', return_value={'features_supported': True}), \
                         mock.patch.object(WORKER, 'read_environment_value', return_value=language), \
                         mock.patch.object(WORKER, 'app_broadcast', return_value={'status': 'returned', 'response': response}) as broadcast, \
                         mock.patch.object(WORKER, 'ugripper_log'):
                        self.assertEqual(WORKER.run_speech(args), 0)
                        broadcast.assert_called_once_with(adb, 'SpeechCommandReceiver', 'SPEECH_PLAY',
                                                          '--ei', 'code', str(code), '--es', 'language', language)
                        response['code'] = 9999
                        self.assertEqual(WORKER.run_speech(args), 1)

    def test_speech_does_not_discover_unbound_egos(self):
        args = argparse.Namespace(binding_file=Path('unused'), serial='', speech_code=1302)
        with mock.patch.object(WORKER, 'read_status', return_value={}), \
             mock.patch.object(WORKER, 'detect_egos') as detect:
            self.assertEqual(WORKER.run_speech(args), 0)
            detect.assert_not_called()

    def test_unknown_speech_code_is_rejected_before_device_access(self):
        with mock.patch.object(WORKER, 'detect_egos') as detect:
            with self.assertRaises(ValueError):
                WORKER.run_speech(argparse.Namespace(speech_code=9999))
            detect.assert_not_called()


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
            result = WORKER.legacy_sync_ego_time(object())

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
            result = WORKER.legacy_sync_ego_time(object())

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
            result = WORKER.legacy_sync_ego_time(object())

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
            result = WORKER.legacy_sync_ego_time(object())

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
            result = WORKER.legacy_sync_ego_time(object())

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
            result = WORKER.legacy_sync_ego_time(object())

        self.assertEqual(result["status"], "error")
        self.assertEqual(result["attempt_count"], 2)
        self.assertEqual(result["final_offset_ms"], -15.0)

    def test_discovers_serial_scoped_dataset_root(self):
        root = "/sdcard/Android/data/com.ssnwt.helloxr/files/1150063703226061200012/data/dataset"
        with mock.patch.dict(WORKER.os.environ, {}, clear=True):
            self.assertEqual(WORKER.list_dataset_roots(FakeAdb([root])), [root])

    def test_stale_preferred_root_does_not_hide_active_fallback_root(self):
        serial = "1150063703226061200012"
        stale_root = f"{WORKER.LEGACY_REMOTE_ROOT}/{serial}/data"
        active_root = "/sdcard/Android/data/com.ssnwt.helloxr/files/another/data/dataset"
        adb = FakeAdb([stale_root, active_root], serial=serial)

        with mock.patch.dict(WORKER.os.environ, {}, clear=True):
            roots = WORKER.list_dataset_roots(adb)
            with mock.patch.object(
                WORKER,
                "list_temp_episodes",
                side_effect=lambda _adb, root: {
                    stale_root: [],
                    active_root: ["episode_20260828_0001-temp"],
                }[root],
            ):
                candidates = WORKER.find_new_temp_episodes(
                    adb, {stale_root: set(), active_root: set()}
                )

        self.assertEqual(roots, [active_root, stale_root])
        self.assertEqual(candidates, [(active_root, "episode_20260828_0001-temp")])
        discovery_commands = [args[1] for args in adb.commands if len(args) == 2 and args[0] == "shell"]
        self.assertTrue(discovery_commands)
        self.assertTrue(all("exit 0" not in command for command in discovery_commands))

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
            selected.run.return_value = subprocess.CompletedProcess([], 0, '', '')
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

    def test_recovery_idle_does_not_stop(self):
        adb = mock.Mock()
        with mock.patch.object(WORKER, "read_recording_state", return_value="idle"):
            WORKER.recover_previous_recording(adb, {}, Path("unused"))
        adb.run.assert_not_called()

    def test_recovery_waits_for_idle_and_rename_preserving_stale_temp(self):
        adb = mock.Mock()
        adb.run.return_value = subprocess.CompletedProcess([], 0, "", "")
        with mock.patch.object(WORKER, "read_recording_state", side_effect=["recording", "recording", "idle"]), mock.patch.object(
            WORKER, "snapshot_temp_episodes", return_value={"/data": {"episode_old-temp", "episode_stale-temp"}}
        ), mock.patch.object(WORKER, "remote_dir_missing", side_effect=lambda a, r, e: e == "episode_old-temp"), mock.patch.object(
            WORKER, "remote_dir_exists", return_value=True
        ), mock.patch.object(WORKER, "write_status"), mock.patch.object(WORKER.time, "sleep"):
            status = {}
            WORKER.recover_previous_recording(adb, status, Path("unused"))
        self.assertEqual(status["status"], "starting")
        self.assertEqual(adb.run.call_count, 1)
        self.assertIn("com.ssnwt.helloxr.STOP_RECORDING", adb.run.call_args.args)

    def test_recovery_timeout_does_not_start_or_delete(self):
        adb = mock.Mock()
        adb.run.return_value = subprocess.CompletedProcess([], 0, "", "")
        with mock.patch.object(WORKER, "read_recording_state", return_value="recording"), mock.patch.object(
            WORKER, "snapshot_temp_episodes", return_value={"/data": {"episode_old-temp"}}
        ), mock.patch.object(WORKER, "write_status"), mock.patch.object(WORKER.time, "monotonic", side_effect=[0, 21]):
            with self.assertRaisesRegex(RuntimeError, "timed out"):
                WORKER.recover_previous_recording(adb, {}, Path("unused"))
        self.assertEqual(adb.run.call_count, 1)

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
            ), mock.patch.object(WORKER, "read_recording_state", return_value="idle"), mock.patch.object(WORKER, "snapshot_temp_episodes", return_value={}), mock.patch.object(
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

    def test_stop_retries_at_exactly_one_hundred_ms_until_success(self):
        for success_attempt in (1, 2, 3):
            with self.subTest(success_attempt=success_attempt), tempfile.TemporaryDirectory() as temp_dir:
                root = Path(temp_dir)
                episode_dir = root / "episode_0001-temp"
                status_file = episode_dir / "ego" / "ego_sync.json"
                status_file.parent.mkdir(parents=True)
                status_file.write_text(
                    json.dumps(
                        {
                            "status": "recording",
                            "serial": "ego-001",
                            "remote_root": "/remote/dataset",
                            "remote_episode": "episode_0001-temp",
                        }
                    ),
                    encoding="utf-8",
                )
                args = argparse.Namespace(
                    serial="ego-001",
                    episode_dir=episode_dir,
                    status_file=status_file,
                    finalize_timeout=1.0,
                    interval=0.01,
                )
                adb = mock.Mock(serial="ego-001")
                adb.run.side_effect = [
                    subprocess.CompletedProcess([], 1, "", "broadcast failed")
                    for _ in range(success_attempt - 1)
                ] + [subprocess.CompletedProcess([], 0, "Broadcast completed", "")]

                with mock.patch.object(WORKER, "Adb", return_value=adb), mock.patch.object(
                    WORKER, "adb_device_ready", return_value=(True, "")
                ), mock.patch.object(
                    WORKER, "remote_dir_exists", side_effect=[False, True]
                ), mock.patch.object(
                    WORKER, "sync_episode_once", return_value={}
                ), mock.patch.object(
                    WORKER,
                    "recover_finalized_episode_files",
                    return_value=(True, {"status": "complete", "attempts": []}, "", ""),
                ), mock.patch.object(
                    WORKER, "refresh_final_mp4_headers", return_value={}
                ), mock.patch.object(
                    WORKER, "repair_mp4_files", return_value={}
                ), mock.patch.object(WORKER.time, "sleep") as sleep:
                    self.assertEqual(WORKER.run_stop(args), 0)

                self.assertEqual(adb.run.call_count, success_attempt)
                self.assertEqual(
                    sleep.call_args_list,
                    [mock.call(WORKER.STOP_BROADCAST_RETRY_INTERVAL_SEC)] * (success_attempt - 1),
                )
                status = json.loads(status_file.read_text(encoding="utf-8"))
                self.assertEqual(status["status"], "finalized")
                self.assertEqual(len(status["stop_attempts"]), success_attempt)

    def test_stop_broadcast_nonzero_three_times_reports_ego_disconnected(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            root = Path(temp_dir)
            episode_dir = root / "episode_0001-temp"
            status_file = episode_dir / "ego" / "ego_sync.json"
            status_file.parent.mkdir(parents=True)
            status_file.write_text(
                json.dumps(
                    {
                        "status": "recording",
                        "serial": "ego-001",
                        "remote_root": "/remote/dataset",
                        "remote_episode": "episode_0001-temp",
                    }
                ),
                encoding="utf-8",
            )
            args = argparse.Namespace(
                serial="ego-001",
                episode_dir=episode_dir,
                status_file=status_file,
                finalize_timeout=1.0,
                interval=0.01,
            )
            adb = mock.Mock(serial="ego-001")
            adb.run.return_value = subprocess.CompletedProcess([], 1, "", "broadcast failed")

            with mock.patch.object(WORKER, "Adb", return_value=adb), mock.patch.object(
                WORKER, "adb_device_ready", return_value=(True, "")
            ), mock.patch.object(WORKER.time, "sleep") as sleep, mock.patch.object(
                WORKER, "ugripper_log"
            ) as log:
                self.assertEqual(WORKER.run_stop(args), 1)

            self.assertEqual(adb.run.call_count, WORKER.STOP_BROADCAST_ATTEMPTS)
            self.assertEqual(
                sleep.call_args_list,
                [mock.call(WORKER.STOP_BROADCAST_RETRY_INTERVAL_SEC)] * 2,
            )
            status = json.loads(status_file.read_text(encoding="utf-8"))
            self.assertEqual(status["status"], "stop_failed")
            self.assertEqual(status["error_type"], "ego_disconnected")
            log.assert_called_with(
                "ERROR", "broadcast failed", component="ego", error_code="UGR-82-01"
            )

    def test_sync_one_file_resumes_from_current_local_size(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            local_dir = Path(temp_dir)
            local_file = local_dir / "sensor.mcap"
            local_file.write_bytes(b"abcd")
            adb = mock.Mock()
            adb.exec_out_bytes.return_value = b"efghij"

            with mock.patch.object(WORKER, "remote_file_size", return_value=10):
                copied = WORKER.sync_one_file(
                    adb,
                    "/remote/dataset",
                    "episode_0001",
                    local_dir,
                    "sensor.mcap",
                )

            self.assertEqual(copied, 6)
            adb.exec_out_bytes.assert_called_once_with(
                "/remote/dataset/episode_0001/sensor.mcap", 4, 6
            )
            self.assertEqual(local_file.read_bytes(), b"abcdefghij")

    def test_interrupted_append_resumes_again_without_duplicate_bytes(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            local_dir = Path(temp_dir)
            local_file = local_dir / "sensor.mcap"
            local_file.write_bytes(b"abcd")
            adb = mock.Mock()
            adb.exec_out_bytes.side_effect = [b"ef", b"ghij"]

            with mock.patch.object(WORKER, "remote_file_size", return_value=10):
                self.assertEqual(
                    WORKER.sync_one_file(
                        adb, "/remote/dataset", "episode_0001", local_dir, "sensor.mcap"
                    ),
                    2,
                )
                self.assertEqual(
                    WORKER.sync_one_file(
                        adb, "/remote/dataset", "episode_0001", local_dir, "sensor.mcap"
                    ),
                    4,
                )

            self.assertEqual(
                adb.exec_out_bytes.call_args_list,
                [
                    mock.call("/remote/dataset/episode_0001/sensor.mcap", 4, 6),
                    mock.call("/remote/dataset/episode_0001/sensor.mcap", 6, 4),
                ],
            )
            self.assertEqual(local_file.read_bytes(), b"abcdefghij")

    def test_finalized_remote_shrink_restarts_only_affected_file(self):
        with tempfile.TemporaryDirectory() as temp_dir:
            local_dir = Path(temp_dir)
            local_file = local_dir / "sensor.mcap"
            other_file = local_dir / "metadata.json"
            local_file.write_bytes(b"stale-data")
            other_file.write_bytes(b"keep")
            adb = mock.Mock()
            adb.exec_out_bytes.return_value = b"new"

            with mock.patch.object(WORKER, "remote_file_size", return_value=3):
                copied = WORKER.sync_one_file(
                    adb,
                    "/remote/dataset",
                    "episode_0001",
                    local_dir,
                    "sensor.mcap",
                    replace_shrunk=True,
                )

            self.assertEqual(copied, 3)
            self.assertEqual(local_file.read_bytes(), b"new")
            self.assertEqual(other_file.read_bytes(), b"keep")

    def test_recovery_retries_size_mismatch_then_succeeds(self):
        adb = mock.Mock()
        mismatch = {"sensor.mcap": {"ok": False, "local_size": 4, "remote_size": 10}}
        matched = {"sensor.mcap": {"ok": True, "local_size": 10, "remote_size": 10}}
        with mock.patch.object(WORKER, "adb_device_ready", return_value=(True, "")), mock.patch.object(
            WORKER, "sync_episode_once", return_value={"sensor.mcap": {"delta": 6}}
        ) as sync, mock.patch.object(
            WORKER,
            "verify_remote_and_local_file_sizes",
            side_effect=[(False, mismatch), (True, matched)],
        ), mock.patch.object(WORKER.time, "sleep") as sleep:
            ok, detail, error, error_type = WORKER.recover_finalized_episode_files(
                adb, "/remote/dataset", "episode_0001", Path("/tmp/local"), attempts=2
            )

        self.assertTrue(ok)
        self.assertEqual(error, "")
        self.assertEqual(error_type, "")
        self.assertEqual(len(detail["attempts"]), 2)
        self.assertEqual(sync.call_count, 2)
        sleep.assert_called_once_with(WORKER.FINAL_SYNC_RETRY_INTERVAL_SEC)

    def test_recovery_missing_remote_data_reports_pull_incomplete(self):
        adb = mock.Mock()
        missing = {"sensor.mcap": {"ok": False, "local_size": 4, "remote_size": None}}
        with mock.patch.object(WORKER, "adb_device_ready", return_value=(True, "")), mock.patch.object(
            WORKER, "sync_episode_once", return_value={"sensor.mcap": {"delta": 0}}
        ), mock.patch.object(
            WORKER, "verify_remote_and_local_file_sizes", return_value=(False, missing)
        ), mock.patch.object(WORKER.time, "sleep"):
            ok, detail, error, error_type = WORKER.recover_finalized_episode_files(
                adb, "/remote/dataset", "episode_0001", Path("/tmp/local"), attempts=2
            )

        self.assertFalse(ok)
        self.assertEqual(detail["status"], "failed")
        self.assertEqual(error_type, "ego_pull_incomplete")
        self.assertIn("missing or incomplete", error)

    def test_recovery_device_unavailable_reports_ego_disconnected(self):
        adb = mock.Mock()
        with mock.patch.object(
            WORKER, "adb_device_ready", return_value=(False, "device offline")
        ), mock.patch.object(WORKER, "sync_episode_once") as sync, mock.patch.object(
            WORKER.time, "sleep"
        ) as sleep:
            ok, detail, error, error_type = WORKER.recover_finalized_episode_files(
                adb, "/remote/dataset", "episode_0001", Path("/tmp/local"), attempts=2
            )

        self.assertFalse(ok)
        self.assertEqual(detail["status"], "failed")
        self.assertEqual(error, "device offline")
        self.assertEqual(error_type, "ego_disconnected")
        sync.assert_not_called()
        sleep.assert_called_once_with(WORKER.FINAL_SYNC_RETRY_INTERVAL_SEC)


if __name__ == "__main__":
    unittest.main()
