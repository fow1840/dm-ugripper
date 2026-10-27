#!/usr/bin/env python3
import importlib.util
import copy
import sys
import tempfile
import unittest
from argparse import Namespace
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
VALIDATOR_PATH = ROOT / ".codex/skills/validate-episode-data/scripts/validate_episode.py"
SPEC = importlib.util.spec_from_file_location("validate_episode", VALIDATOR_PATH)
VALIDATOR = importlib.util.module_from_spec(SPEC)
assert SPEC.loader is not None
sys.modules[SPEC.name] = VALIDATOR
SPEC.loader.exec_module(VALIDATOR)


NOSTEREO_VIDEO_NAMES = [
    "cam_left.mkv",
    "tcam_left_l.mkv",
    "tcam_left_r.mkv",
    "cam_right.mkv",
    "tcam_right_l.mkv",
    "tcam_right_r.mkv",
]
STEREO_VIDEO_NAMES = [
    "cam_left.mkv",
    "tcam_left_l.mkv",
    "tcam_left_r.mkv",
    "stereo_left.mkv",
    "cam_right.mkv",
    "tcam_right_l.mkv",
    "tcam_right_r.mkv",
    "stereo_right.mkv",
]
UNIX_BASE_US = 1_785_000_000_000_000


def hardware_list(stereo_enabled):
    return {
        "gripper_right_sn": "DAG-R",
        "gripper_left_sn": "DAG-L",
        "cam_right_sn": "FE-R",
        "cam_left_sn": "FE-L",
        "cam_chest_sn": "",
        "tactile_right_l_sn": "TR-L",
        "tactile_right_r_sn": "TR-R",
        "tactile_left_l_sn": "TL-L",
        "tactile_left_r_sn": "TL-R",
        "stereo_right_sn": "ST-R" if stereo_enabled else "",
        "stereo_left_sn": "ST-L" if stereo_enabled else "",
    }


def video_details(names):
    return [
        {
            "name": name,
            "fps": 60.0,
            "duration_s": 10.0,
            "start_offset_us": UNIX_BASE_US + index * 1000,
        }
        for index, name in enumerate(names)
    ]


def metadata(stereo_enabled=False, data_version="3.3", marker_values=None):
    required_files = list(VALIDATOR.CORE_REQUIRED_FILES)
    names = NOSTEREO_VIDEO_NAMES
    if stereo_enabled:
        required_files.extend(VALIDATOR.STEREO_REQUIRED_FILES)
        names = STEREO_VIDEO_NAMES
    result = {
        "device_sn": "DAP-TEST",
        "device_type": "ugripper",
        "device_mode": "dual",
        "camera_codec": "h265",
        "hardware_version": "v2.5",
        "software_version": "v2.1.9" if stereo_enabled else "v2.1.9+nostereo",
        "das_usb_updater_version": "1.0.3",
        "data_version": data_version,
    }
    if data_version == "3.3":
        for key in VALIDATOR.METADATA_33_VERSION_FIELDS:
            if key == "ego_software_version": result["ego_bound"] = False
            result[key] = "" if key.startswith("ego_") else "1.0.0"
        result["cam_chest_firmware_version"] = "disabled"
        if not stereo_enabled:
            for side in ("left", "right"):
                result[f"stereo_{side}_firmware_version"] = "disabled"
    result.update({
        "hardware_list": hardware_list(stereo_enabled),
        "episode_name": "episode_20260727_0001",
        "data_uuid": "test-uuid",
        "audio_uuid": "",
        "quality_check_status": "success",
        "quality_check_err_type": "",
        "collection_duration_s": 10.0,
    })
    if marker_values is not None:
        result.update(marker_values)
    if data_version not in {"3.0", "3.1"}:
        result["boot_time_s"] = 1_785_000_000.0
        result["tactile_damage_sn_list"] = []
    if data_version == "3.3": result["hardware_list"]["ego_sn"] = ""
    result["require_files"] = required_files
    result["video_details"] = video_details(names)
    return result


class ValidateEpisodeProfileTest(unittest.TestCase):
    def make_validator(self, episode_dir):
        return VALIDATOR.EpisodeValidator(Namespace(path=str(episode_dir), latest=False))

    def write_required_files(self, episode_dir, stereo_enabled):
        required_files = list(VALIDATOR.CORE_REQUIRED_FILES)
        if stereo_enabled:
            required_files.extend(VALIDATOR.STEREO_REQUIRED_FILES)
        for name in required_files:
            (episode_dir / name).write_bytes(b"x")

    def validate_metadata(self, payload):
        with tempfile.TemporaryDirectory(prefix="ugripper-validator-") as tmp:
            episode_dir = Path(tmp) / "episode_20260727_0001"
            episode_dir.mkdir()
            validator = self.make_validator(episode_dir)
            validator.metadata = payload
            validator.resolve_episode_profile()
            validator.resolve_active_video_files()
            validator.validate_metadata_fields()
            return validator

    def populate_global_stats(self, validator, stereo_enabled):
        validator.stereo_enabled = stereo_enabled
        validator.active_video_files = [
            entry
            for entry in VALIDATOR.BASE_VIDEO_FILES
            if stereo_enabled or entry[0] not in {"left_stereo", "right_stereo"}
        ]
        for index, (video_name, file_name, side) in enumerate(validator.active_video_files):
            validator.video_stats[video_name] = VALIDATOR.VideoStats(
                name=video_name,
                side=side,
                path=file_name,
                first_system_time_us=UNIX_BASE_US + index * 10_000,
            )
        validator.sensor_stats["encoder_left"] = VALIDATOR.SensorTopicStats(
            topic="encoder_left",
            source_file="sensor_left.mcap",
            first_publish_time_ns=(UNIX_BASE_US + 80_000) * 1000,
        )
        validator.sensor_stats["encoder_right"] = VALIDATOR.SensorTopicStats(
            topic="encoder_right",
            source_file="sensor_right.mcap",
            first_publish_time_ns=(UNIX_BASE_US + 90_000) * 1000,
        )
        if stereo_enabled:
            validator.fays_mcap_stats["fays_data_left.mcap"] = VALIDATOR.FaysMcapStats(
                side="left",
                source_file="fays_data_left.mcap",
                first_log_time_ns=123,
                first_imu_publish_time_ns=(UNIX_BASE_US + 100_000) * 1000,
                first_camera_publish_time_ns=(UNIX_BASE_US + 30_000) * 1000,
            )
            validator.fays_mcap_stats["fays_data_right.mcap"] = VALIDATOR.FaysMcapStats(
                side="right",
                source_file="fays_data_right.mcap",
                first_log_time_ns=456,
                first_imu_publish_time_ns=(UNIX_BASE_US + 110_000) * 1000,
                first_camera_publish_time_ns=(UNIX_BASE_US + 70_000) * 1000,
            )

    def test_nostereo_profile_does_not_require_stereo_or_fays(self):
        with tempfile.TemporaryDirectory(prefix="ugripper-validator-") as tmp:
            episode_dir = Path(tmp) / "episode_20260727_0001"
            episode_dir.mkdir()
            self.write_required_files(episode_dir, stereo_enabled=False)
            validator = self.make_validator(episode_dir)
            validator.metadata = metadata(stereo_enabled=False)
            validator.resolve_episode_profile()
            validator.resolve_active_video_files()
            validator.check_required_files()
            validator.validate_metadata_fields()

            self.assertFalse(validator.stereo_enabled)
            self.assertFalse(any(item.severity == "FAIL" for item in validator.findings))
            self.assertNotIn("left_stereo", {entry[0] for entry in validator.active_video_files})

    def test_stereo_profile_still_requires_stereo_and_fays(self):
        with tempfile.TemporaryDirectory(prefix="ugripper-validator-") as tmp:
            episode_dir = Path(tmp) / "episode_20260727_0001"
            episode_dir.mkdir()
            self.write_required_files(episode_dir, stereo_enabled=False)
            validator = self.make_validator(episode_dir)
            validator.metadata = metadata(stereo_enabled=True)
            validator.resolve_episode_profile()
            validator.resolve_active_video_files()
            validator.check_required_files()

            missing = {
                item.details.get("path", "").rsplit("/", 1)[-1]
                for item in validator.findings
                if item.code == "missing_file"
            }
            self.assertEqual(missing, set(VALIDATOR.STEREO_REQUIRED_FILES))

    def test_task_markers_are_optional_and_start_only_means_no_loop_closure(self):
        cases = [
            (None, None),
            ({"task_start_s": 0.0, "task_stop_s": 0.0}, None),
            ({"task_start_s": 1000.0}, "metadata_task_start_only"),
            ({"task_start_s": 1000.0, "task_stop_s": 0.0}, "metadata_task_start_only"),
            ({"task_start_s": 1000.0, "task_stop_s": 1010.0}, None),
        ]
        for marker_values, expected_info_code in cases:
            with self.subTest(marker_values=marker_values):
                validator = self.validate_metadata(metadata(marker_values=marker_values))
                self.assertFalse(any(item.severity == "FAIL" for item in validator.findings))
                info_codes = {item.code for item in validator.findings if item.severity == "INFO"}
                if expected_info_code is not None:
                    self.assertIn(expected_info_code, info_codes)

    def test_invalid_task_marker_order_fails(self):
        cases = [
            {"task_stop_s": 1010.0},
            {"task_start_s": -1.0, "task_stop_s": 0.0},
            {"task_start_s": 1010.0, "task_stop_s": 1000.0},
            {"task_start_s": 1000.0, "task_stop_s": 1000.0},
        ]
        for marker_values in cases:
            with self.subTest(marker_values=marker_values):
                validator = self.validate_metadata(metadata(marker_values=marker_values))
                self.assertTrue(
                    any(item.code == "metadata_task_markers" and item.severity == "FAIL" for item in validator.findings)
                )

    def test_tactile_damage_sn_list_accepts_empty_and_single_sn(self):
        for damaged_serial_numbers in ([], ["TR-R"]):
            with self.subTest(damaged_serial_numbers=damaged_serial_numbers):
                payload = metadata()
                payload["tactile_damage_sn_list"] = damaged_serial_numbers
                validator = self.validate_metadata(payload)
                self.assertFalse(any(item.severity == "FAIL" for item in validator.findings))

    def test_invalid_metadata_3_2_damage_list_or_boot_time_fails(self):
        cases = [
            ("tactile_damage_sn_list", ["TR-R", "TR-R"]),
            ("tactile_damage_sn_list", [""]),
            ("tactile_damage_sn_list", [123]),
            ("boot_time_s", True),
            ("boot_time_s", 1.0),
        ]
        for field_name, value in cases:
            with self.subTest(field_name=field_name, value=value):
                payload = metadata()
                payload[field_name] = value
                validator = self.validate_metadata(payload)
                self.assertTrue(any(item.severity == "FAIL" for item in validator.findings))

    def test_data_versions_3_0_and_3_1_are_accepted_as_legacy(self):
        for data_version in ("3.0", "3.1"):
            with self.subTest(data_version=data_version):
                validator = self.validate_metadata(metadata(data_version=data_version))
                self.assertFalse(any(item.code == "metadata_data_version" for item in validator.findings))
                self.assertTrue(any(item.code == "metadata_data_version_legacy" for item in validator.findings))

    def test_metadata_3_3_preserves_all_non_version_fields(self):
        for stereo in (False, True):
            current = metadata(stereo_enabled=stereo)
            historical = metadata(stereo_enabled=stereo, data_version="3.2")
            stripped = {k: v for k, v in current.items() if k not in {*VALIDATOR.METADATA_33_VERSION_FIELDS, "ego_bound"}}
            stripped["hardware_list"] = {k:v for k,v in stripped["hardware_list"].items() if k != "ego_sn"}
            stripped["data_version"] = "3.2"
            self.assertEqual(list(stripped), list(historical))
            self.assertEqual(stripped, historical)
            for payload in (current, historical):
                self.assertFalse(any(f.severity == "FAIL" for f in self.validate_metadata(payload).findings))

    def test_metadata_3_3_rejects_version_type_missing_and_order_drift(self):
        for mode in ("type", "missing", "order"):
            payload = metadata()
            field = "stereo_left_firmware_version"
            if mode == "type": payload[field] = 123
            elif mode == "missing": del payload[field]
            else: payload[field] = payload.pop(field)
            self.assertTrue(any(f.severity == "FAIL" for f in self.validate_metadata(payload).findings))

    def test_metadata_3_3_requires_disabled_chest_and_nostereo(self):
        for field in ("cam_chest_firmware_version", "stereo_left_firmware_version", "stereo_right_firmware_version"):
            payload = metadata()
            payload[field] = ""
            self.assertTrue(any(f.code == "metadata_version_disabled" for f in self.validate_metadata(payload).findings))
        payload = metadata(stereo_enabled=True)
        payload["stereo_right_firmware_version"] = "disabled"
        self.assertTrue(any(f.code == "metadata_version_disabled" for f in self.validate_metadata(payload).findings))

    def test_metadata_3_3_template_matches_writer_contract(self):
        import json
        import re
        payload = json.loads((ROOT / ".codex/skills/validate-episode-data/references/metadata-v3.3.json").read_text())
        header = (ROOT / "standalone/UgripperRuntime/include/record_runtime/episode_types.h").read_text()
        field_block = header.split("kDeviceVersionFields = {{", 1)[1].split("}};", 1)[0]
        self.assertEqual(re.findall(r'"([^"\n]+)"', field_block), VALIDATOR.METADATA_33_VERSION_FIELDS)
        self.assertEqual(list(payload), VALIDATOR.EXPECTED_METADATA_KEY_ORDER)
        self.assertFalse(any(f.severity == "FAIL" for f in self.validate_metadata(payload).findings))

    def test_test_dataset_name_order_values_and_legacy(self):
        original = metadata()
        for value in ('', 'trajectory_test', 'production_test'):
            payload = {}
            for key, item in original.items():
                payload[key] = item
                if key == 'episode_name': payload['test_dataset_name'] = value
            self.assertFalse(any(f.severity=='FAIL' for f in self.validate_metadata(payload).findings))
        for value in (None, 1, [], 'unknown'):
            invalid = dict(payload, test_dataset_name=value)
            self.assertTrue(any(f.code=='metadata_test_dataset_name' for f in self.validate_metadata(invalid).findings))
        self.assertFalse(any(f.severity=='FAIL' for f in self.validate_metadata(original).findings))
        reordered = dict(original, test_dataset_name='trajectory_test')
        self.assertTrue(any(f.code=='metadata_schema_key_order' for f in self.validate_metadata(reordered).findings))

    def test_metadata_3_3_additive_board_version_preserves_legacy(self):
        current = metadata()
        historical = dict(current)
        del historical["rk3588_firmware_version"]
        for payload in (current, historical):
            self.assertFalse(any(f.severity == "FAIL" for f in self.validate_metadata(payload).findings))
        for value in ("", "unknown", 123):
            payload = dict(current, rk3588_firmware_version=value)
            self.assertTrue(any(f.severity == "FAIL" for f in self.validate_metadata(payload).findings))
        reordered = dict(historical, rk3588_firmware_version=current["rk3588_firmware_version"])
        self.assertTrue(any(f.severity == "FAIL" for f in self.validate_metadata(reordered).findings))

    def test_metadata_3_3_binding_and_required_version_consistency(self):
        for field, value in (("ego_bound", "false"), ("ego_software_version", "1.0.5"), ("gripper_left_firmware_version", ""), ("cam_left_firmware_version", "unknown")):
            payload = metadata()
            payload[field] = value
            self.assertTrue(any(f.severity == "FAIL" for f in self.validate_metadata(payload).findings))
        payload = metadata()
        payload["hardware_list"]["ego_sn"] = "ego-sn"
        self.assertTrue(any(f.code == "metadata_ego_binding" for f in self.validate_metadata(payload).findings))
        payload["ego_bound"] = True
        self.assertTrue(any(f.code == "metadata_version_missing" for f in self.validate_metadata(payload).findings))
        payload["ego_software_version"] = "1.0.5"
        payload["ego_firmware_version"] = "build-1"
        self.assertFalse(any(f.severity == "FAIL" for f in self.validate_metadata(payload).findings))

    def test_ego_linkage_matches_parent_and_legacy_is_compatible(self):
        import json
        with tempfile.TemporaryDirectory() as folder:
            directory = Path(folder) / "episode_20261012_0001"
            directory.mkdir()
            validator = self.make_validator(directory)
            validator.metadata = metadata()
            validator.metadata["ego_bound"] = True
            validator.validate_ego_linkage()
            self.assertTrue(any(f.severity == "FAIL" for f in validator.findings))
            (directory / "ego").mkdir()
            ego = {"related_episode_name": validator.metadata["episode_name"], "related_episode_uuid": validator.metadata["data_uuid"]}
            (directory / "ego/metadata.json").write_text(json.dumps(ego))
            validator.ego_metadata = ego
            validator.findings = []
            validator.validate_ego_linkage()
            self.assertEqual(validator.findings, [])
            ego["related_episode_uuid"] = "different"
            validator.validate_ego_linkage()
            self.assertTrue(any(f.code == "metadata_ego_linkage" for f in validator.findings))
            validator.findings = []
            validator.metadata["data_version"] = "3.2"
            validator.validate_ego_linkage()
            self.assertEqual(validator.findings, [])

    def test_relative_video_offsets_fail_unix_time_validation(self):
        payload = metadata()
        for index, detail in enumerate(payload["video_details"]):
            detail["start_offset_us"] = index
        validator = self.validate_metadata(payload)
        self.assertTrue(
            any(item.code == "metadata_video_time_domain" and item.severity == "FAIL" for item in validator.findings)
        )

    def test_global_first_frame_sync_lists_all_nostereo_streams(self):
        with tempfile.TemporaryDirectory(prefix="ugripper-validator-") as tmp:
            episode_dir = Path(tmp) / "episode_20260727_0001"
            episode_dir.mkdir()
            validator = self.make_validator(episode_dir)
            self.populate_global_stats(validator, stereo_enabled=False)
            validator.check_video_sensor_alignment()

            result = validator.global_first_frame_sync
            self.assertEqual(result["status"], "PASS")
            self.assertEqual(result["fail_threshold_ms"], 1000.0)
            self.assertEqual(len(result["streams"]), len(NOSTEREO_VIDEO_NAMES) + 2)
            self.assertFalse(result["missing_streams"])

    def test_global_first_frame_sync_uses_fays_publish_time(self):
        with tempfile.TemporaryDirectory(prefix="ugripper-validator-") as tmp:
            episode_dir = Path(tmp) / "episode_20260727_0001"
            episode_dir.mkdir()
            validator = self.make_validator(episode_dir)
            self.populate_global_stats(validator, stereo_enabled=True)
            validator.check_video_sensor_alignment()

            result = validator.global_first_frame_sync
            self.assertEqual(result["status"], "PASS")
            names = {item["name"] for item in result["streams"]}
            self.assertIn("fays:left:imu", names)
            self.assertIn("fays:right:camera", names)
            self.assertNotIn("fays_data_left.mcap:logTime", names)

    def test_global_first_frame_sync_fails_at_one_second(self):
        with tempfile.TemporaryDirectory(prefix="ugripper-validator-") as tmp:
            episode_dir = Path(tmp) / "episode_20260727_0001"
            episode_dir.mkdir()
            validator = self.make_validator(episode_dir)
            self.populate_global_stats(validator, stereo_enabled=False)
            validator.sensor_stats["encoder_right"].first_publish_time_ns = (UNIX_BASE_US + 1_000_000) * 1000
            validator.check_video_sensor_alignment()

            self.assertEqual(validator.global_first_frame_sync["status"], "FAIL")
            self.assertEqual(validator.global_first_frame_sync["max_error_ms"], 1000.0)
            self.assertTrue(
                any(item.code == "global_first_frame_sync" and item.severity == "FAIL" for item in validator.findings)
            )

    def test_global_first_frame_sync_includes_optional_ego_topics(self):
        with tempfile.TemporaryDirectory(prefix="ugripper-validator-") as tmp:
            episode_dir = Path(tmp) / "episode_20260727_0001"
            episode_dir.mkdir()
            validator = self.make_validator(episode_dir)
            self.populate_global_stats(validator, stereo_enabled=False)
            validator.sensor_stats["ego:tracking"] = VALIDATOR.SensorTopicStats(
                topic="ego:tracking",
                source_file="ego/sensor.mcap",
                first_publish_time_ns=(UNIX_BASE_US + 120_000) * 1000,
            )
            validator.check_video_sensor_alignment()

            stream_names = {item["name"] for item in validator.global_first_frame_sync["streams"]}
            self.assertIn("sensor:ego:tracking", stream_names)

    def test_ego_monotonic_topics_use_head_pose_metadata_offset(self):
        with tempfile.TemporaryDirectory(prefix="ugripper-validator-") as tmp:
            episode_dir = Path(tmp) / "episode_20260727_0001"
            episode_dir.mkdir()
            validator = self.make_validator(episode_dir)
            raw_head_pose_us = 5_522_316_243
            head_pose_unix_us = UNIX_BASE_US + 120_000
            validator.ego_metadata = {
                "head_pose_details": [
                    {"name": "head_pose", "start_offset_us": head_pose_unix_us}
                ]
            }
            validator.sensor_stats["ego:head_pose"] = VALIDATOR.SensorTopicStats(
                topic="ego:head_pose",
                source_file="ego/sensor.mcap",
                first_publish_time_ns=raw_head_pose_us * 1000,
            )
            validator.sensor_stats["ego:imu/gyro"] = VALIDATOR.SensorTopicStats(
                topic="ego:imu/gyro",
                source_file="ego/sensor.mcap",
                first_publish_time_ns=(raw_head_pose_us + 13_888) * 1000,
            )
            validator.sensor_stats["ego:rgb_metainfo"] = VALIDATOR.SensorTopicStats(
                topic="ego:rgb_metainfo",
                source_file="ego/sensor.mcap",
                first_publish_time_ns=(UNIX_BASE_US + 60_000) * 1000,
            )

            validator.align_sensor_first_frames()

            self.assertEqual(
                validator.sensor_stats["ego:head_pose"].first_system_time_us,
                head_pose_unix_us,
            )
            self.assertEqual(
                validator.sensor_stats["ego:imu/gyro"].first_system_time_us,
                head_pose_unix_us + 13_888,
            )
            self.assertEqual(
                validator.sensor_stats["ego:rgb_metainfo"].first_system_time_us,
                UNIX_BASE_US + 60_000,
            )
            self.assertEqual(
                validator.sensor_stats["ego:head_pose"].time_alignment_method,
                "ego_head_pose_metadata_offset",
            )

    def test_global_first_frame_sync_includes_all_discovered_sensor_topics(self):
        with tempfile.TemporaryDirectory(prefix="ugripper-validator-") as tmp:
            episode_dir = Path(tmp) / "episode_20260727_0001"
            episode_dir.mkdir()
            validator = self.make_validator(episode_dir)
            self.populate_global_stats(validator, stereo_enabled=False)
            validator.sensor_stats["imu_left"] = VALIDATOR.SensorTopicStats(
                topic="imu_left",
                source_file="sensor_left.mcap",
                first_publish_time_ns=(UNIX_BASE_US + 100_000) * 1000,
            )

            validator.check_video_sensor_alignment()

            stream_names = {item["name"] for item in validator.global_first_frame_sync["streams"]}
            self.assertIn("sensor:imu_left", stream_names)

    def test_ego_monotonic_topics_fail_without_metadata_offset(self):
        with tempfile.TemporaryDirectory(prefix="ugripper-validator-") as tmp:
            episode_dir = Path(tmp) / "episode_20260727_0001"
            episode_dir.mkdir()
            validator = self.make_validator(episode_dir)
            validator.sensor_stats["ego:head_pose"] = VALIDATOR.SensorTopicStats(
                topic="ego:head_pose",
                source_file="ego/sensor.mcap",
                first_publish_time_ns=5_522_316_243_000,
            )

            validator.align_sensor_first_frames()

            self.assertIsNone(validator.sensor_stats["ego:head_pose"].first_system_time_us)
            self.assertTrue(
                any(item.code == "ego_time_alignment" and item.severity == "FAIL" for item in validator.findings)
            )


def calibration_payload(version="3.1", chest=False):
    info = {"calibration_status": "calibrated"}
    if version == "3.1":
        info["main_cam_extrinsic_calib_status"] = "uncalibrated"
    info["format_version"] = version
    main = dict(camera_model="pinhole", distortion_coeffs=[0, 0, 0, 0],
                distortion_model="equidistant", fps=60, intrinsics=[500, 500, 960, 540],
                names=["height", "width", "channels"], shape=[1080, 1920, 3])
    stereo = dict(cam0=dict(distortion_coeffs=[], intrinsics=[]),
                  cam1=dict(distortion_coeffs=[], intrinsics=[]), camera_model="pinhole",
                  distortion_model="equidistant", extrinsics={}, fps=25,
                  names=["height", "width", "channels"], shape=[800, 640, 3])
    tactile = dict(names=["height", "width", "channels"], serial="TACTILE-SN", shape=[480, 640, 3])
    images = {}
    if chest:
        images["cam_chest_main"] = copy.deepcopy(main)
    for side in ("left", "right"):
        images[f"stereo_{side}"] = copy.deepcopy(stereo)
        for slot in ("l", "r"):
            images[f"tcam_{side}_{slot}"] = copy.deepcopy(tactile)
    # Use a non-identity transform with translation; do not require zero or invert it.
    transform = [[0, -1, 0, .078813], [1, 0, 0, 0], [0, 0, 1, -.025092], [0, 0, 0, 1]]
    for side in ("left", "right"):
        entry = copy.deepcopy(main)
        if version == "3.1":
            entry["extrinsics"] = {name: copy.deepcopy(transform) for name in
                                    ("T_base_main_camera", "T_main_camera_stereo_cam0")}
        images[f"cam_{side}_main"] = entry
    imu = {f"imu_{side}": dict(update_rate_hz=1000, accelerometer={}, gyroscope={})
           for side in ("left", "right")}
    return dict(calibration_info=info, observation=dict(images=images, imu=imu))


class ValidateCalibrationFormatTest(unittest.TestCase):
    def validate(self, payload, data_version="3.2", stereo=True):
        with tempfile.TemporaryDirectory(prefix="ugripper-calibration-") as tmp:
            root = Path(tmp) / "episode_20260917_0001"
            root.mkdir()
            if "cam_chest_main" in payload.get("observation", {}).get("images", {}):
                (root / "cam_chest.mkv").touch()
            validator = VALIDATOR.EpisodeValidator(Namespace(path=str(root), latest=False))
            validator.metadata = metadata(stereo_enabled=stereo, data_version=data_version)
            validator.calibration = payload
            validator.resolve_episode_profile()
            validator.validate_calibration_structure()
            return [item for item in validator.findings if item.severity == "FAIL"]

    def test_legacy_and_current_formats_are_independent_of_metadata_and_profile(self):
        for version in ("3.0", "3.1"):
            for data_version in ("3.0", "3.1", "3.2"):
                for stereo in (False, True):
                    for chest in (False, True):
                        with self.subTest(version=version, metadata=data_version, stereo=stereo, chest=chest):
                            self.assertEqual(self.validate(calibration_payload(version, chest), data_version, stereo), [])

    def test_current_accepts_both_extrinsic_calibration_states(self):
        for status in ("calibrated", "uncalibrated"):
            payload = calibration_payload()
            payload["calibration_info"]["main_cam_extrinsic_calib_status"] = status
            self.assertEqual(self.validate(payload), [])

    def test_missing_invalid_and_unknown_info_is_rejected(self):
        cases = [("format_version", value) for value in (None, "3.2", 3.1, {})]
        cases += [("main_cam_extrinsic_calib_status", value) for value in (None, "unknown", True, {})]
        for field, value in cases:
            with self.subTest(field=field, value=value):
                payload = calibration_payload()
                if value is None:
                    del payload["calibration_info"][field]
                else:
                    payload["calibration_info"][field] = value
                self.assertTrue(self.validate(payload))

    def test_schema_order_and_unknown_fields_remain_strict(self):
        for target in ("info", "main"):
            payload = calibration_payload()
            entry = (payload["calibration_info"] if target == "info" else
                     payload["observation"]["images"]["cam_left_main"])
            first = next(iter(entry))
            entry[first] = entry.pop(first)
            self.assertTrue(any(f.code == "calibration_schema_key_order" for f in self.validate(payload)))
            entry["unexpected"] = 1
            self.assertTrue(self.validate(payload))

    def test_both_main_cameras_require_exactly_two_extrinsics(self):
        for side in ("left", "right"):
            for value in (None, [], {}, {"T_base_main_camera": []},
                          calibration_payload()["observation"]["images"]["cam_left_main"]["extrinsics"] | {"unknown": []}):
                with self.subTest(side=side, value=value):
                    payload = calibration_payload()
                    entry = payload["observation"]["images"][f"cam_{side}_main"]
                    if value is None:
                        del entry["extrinsics"]
                    else:
                        entry["extrinsics"] = value
                    self.assertTrue(any(f.code == "calibration_extrinsics" for f in self.validate(payload)))

    def test_malformed_nonfinite_and_nonrigid_matrices_are_rejected(self):
        valid = calibration_payload()["observation"]["images"]["cam_left_main"]["extrinsics"]["T_base_main_camera"]
        matrices = [None, [], [[1, 0, 0]] * 4]
        for row, col, value in ((0, 0, True), (0, 0, "1"), (0, 0, float("nan")),
                                (0, 0, float("inf")), (0, 0, 10**400), (0, 0, 2),
                                (3, 3, 0), (3, 0, .1), (2, 2, -1)):
            matrix = copy.deepcopy(valid)
            matrix[row][col] = value
            matrices.append(matrix)
        for name in ("T_base_main_camera", "T_main_camera_stereo_cam0"):
            for matrix in matrices:
                with self.subTest(name=name, matrix=matrix):
                    payload = calibration_payload()
                    payload["observation"]["images"]["cam_right_main"]["extrinsics"][name] = matrix
                    self.assertTrue(any(f.code == "calibration_extrinsics" for f in self.validate(payload)))

    def test_legacy_and_chest_do_not_accept_new_extrinsics(self):
        for version, camera in (("3.0", "cam_left_main"), ("3.1", "cam_chest_main")):
            payload = calibration_payload(version, chest=True)
            payload["observation"]["images"][camera]["extrinsics"] = {}
            self.assertTrue(any(f.code == "calibration_schema_unexpected_keys" for f in self.validate(payload)))


if __name__ == "__main__":
    unittest.main()
