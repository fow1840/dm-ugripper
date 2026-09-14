from __future__ import annotations

import hashlib
import stat
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "production"))

import main_camera_dfu as dfu


def executable(path: Path) -> Path:
    path.write_text("#!/bin/sh\nexit 0\n", encoding="utf-8")
    path.chmod(path.stat().st_mode | stat.S_IXUSR)
    return path


class FirmwareInspectionTests(unittest.TestCase):
    def test_path_must_be_an_img_inside_allowlisted_directory(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            allowed = root / "firmware"
            allowed.mkdir()
            good = allowed / "camera.img"
            good.write_bytes(b"firmware")
            outside = root / "outside.img"
            outside.write_bytes(b"outside")

            self.assertEqual(dfu.validate_firmware_path("camera.img", allowed), good)
            with self.assertRaisesRegex(dfu.DfuError, "只能选择") as raised:
                dfu.validate_firmware_path(outside, allowed)
            self.assertEqual(raised.exception.code, "firmware_not_allowed")

            symlink = allowed / "escape.img"
            symlink.symlink_to(outside)
            with self.assertRaises(dfu.DfuError) as symlink_error:
                dfu.validate_firmware_path(symlink, allowed)
            self.assertEqual(symlink_error.exception.code, "firmware_not_allowed")

    def test_dual_version_output_is_parsed_by_label(self) -> None:
        output = (
            "待升级固件版本: 2026-09-02-0001\n"
            "当前 UVC 摄像头版本 (/dev/video0): 2026-09-01-1220\n"
        )
        self.assertEqual(dfu.parse_image_version(output), "2026-09-02-0001")
        self.assertEqual(dfu.parse_current_camera_version(output), "2026-09-01-1220")

    def test_inspection_has_size_hash_version_and_explicit_device(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "iq07_test.img"
            image.write_bytes(b"image payload")
            reader = executable(root / "linux_read_version")
            commands: list[list[str]] = []

            def runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                commands.append(command)
                return subprocess.CompletedProcess(
                    command,
                    0,
                    "待升级固件版本: 2026-09-02-0001\n"
                    "当前 UVC 摄像头版本 (/dev/cam_left): 2026-09-01-1220\n",
                    "",
                )

            info = dfu.inspect_firmware(
                image,
                firmware_dir=root,
                version_reader=reader,
                device_path="/dev/cam_left",
                runner=runner,
            )

            self.assertEqual(info.size, len(b"image payload"))
            self.assertEqual(info.sha256, hashlib.sha256(b"image payload").hexdigest())
            self.assertEqual(info.version, "2026-09-02-0001")
            self.assertEqual(
                commands[0],
                [str(reader.resolve()), "--image", str(image), "--device", "/dev/cam_left"],
            )

    def test_missing_version_reader_is_structured_prerequisite(self) -> None:
        with self.assertRaises(dfu.PrerequisiteError) as raised:
            dfu.find_version_reader("/definitely/missing/linux_read_version")
        self.assertEqual(raised.exception.code, "version_reader_missing")
        self.assertEqual(raised.exception.to_dict()["state"], "prerequisites")


class UsbDiscoveryTests(unittest.TestCase):
    def test_uvc_side_resolves_usb_device_physical_port(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            usb = root / "devices" / "pci" / "usb9" / "9-1.2.1"
            interface = usb / "9-1.2.1:1.0" / "video4linux" / "video7"
            interface.mkdir(parents=True)
            (usb / "idVendor").write_text("27c2\n", encoding="ascii")
            (usb / "idProduct").write_text("0530\n", encoding="ascii")
            class_dir = root / "class" / "video4linux"
            class_dir.mkdir(parents=True)
            (class_dir / "video7").symlink_to(interface)

            port = dfu.uvc_side_to_usb_port(
                "left",
                device_by_side={"left": "/dev/cam_left"},
                sys_class_video=class_dir,
                realpath=lambda _: "/dev/video7",
            )

            self.assertEqual(port, "9-1.2.1")

    def test_uvc_side_rejects_unexpected_usb_identity(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            usb = root / "devices" / "pci" / "usb9" / "9-1.2.1"
            interface = usb / "9-1.2.1:1.0" / "video4linux" / "video7"
            interface.mkdir(parents=True)
            (usb / "idVendor").write_text("0bda\n", encoding="ascii")
            (usb / "idProduct").write_text("5846\n", encoding="ascii")
            class_dir = root / "class" / "video4linux"
            class_dir.mkdir(parents=True)
            (class_dir / "video7").symlink_to(interface)

            with self.assertRaises(dfu.DfuError) as raised:
                dfu.uvc_side_to_usb_port(
                    "left",
                    device_by_side={"left": "/dev/cam_left"},
                    sys_class_video=class_dir,
                    realpath=lambda _: "/dev/video7",
                )

            self.assertEqual(raised.exception.code, "uvc_usb_identity_mismatch")

    def test_parse_dfu_list_filters_vid_pid_and_keeps_physical_path(self) -> None:
        output = (
            'Found DFU: [0955:701a] ver=0100, devnum=12, cfg=1, intf=0, '
            'path="9-1.2.1", alt=0, name="camera"\n'
            'Found DFU: [1234:5678] path="1-2", alt=0, name="other"\n'
        )
        self.assertEqual(dfu.parse_dfu_util_list(output), [dfu.DfuDevice("9-1.2.1", alt=0, description="camera")])

    def test_sysfs_enumeration_filters_target_id(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            target = root / "9-1.2.1"
            target.mkdir()
            (target / "idVendor").write_text("0955", encoding="ascii")
            (target / "idProduct").write_text("701a", encoding="ascii")
            other = root / "1-2"
            other.mkdir()
            (other / "idVendor").write_text("27c2", encoding="ascii")
            (other / "idProduct").write_text("0530", encoding="ascii")

            self.assertEqual(dfu.enumerate_dfu_sysfs(root), [dfu.DfuDevice("9-1.2.1")])

    def test_flash_command_is_always_bound_to_port_and_alt_zero(self) -> None:
        command = dfu.build_dfu_command("/usr/bin/dfu-util", "9-1.2.1", "/fw/cam.img")
        self.assertEqual(
            command,
            [
                "/usr/bin/dfu-util", "-d", "0955:701a", "-p", "9-1.2.1",
                "-a", "0", "-D", "/fw/cam.img", "-R",
            ],
        )


class DfuStateMachineTests(unittest.TestCase):
    def test_matching_version_skips_dfu_without_write_prerequisites(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "camera.img"
            image.write_bytes(b"same firmware")
            reader = executable(root / "linux_read_version")
            commands: list[list[str]] = []
            events: list[dict[str, object]] = []

            def runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                commands.append(command)
                return subprocess.CompletedProcess(
                    command, 0, "待升级固件版本: 2026-07-09-0001\n", ""
                )

            updater = dfu.MainCameraDfuUpdater(
                firmware_dir=root,
                version_reader=reader,
                dfu_trigger=root / "missing-trigger",
                dfu_util=root / "missing-dfu-util",
                runner=runner,
                event_sink=events.append,
                locate_uvc_port=lambda side: self.fail("matching version must not locate DFU port"),
                enumerate_dfu=lambda: self.fail("matching version must not enumerate DFU"),
                read_device_version=lambda side: "2026-07-09-0001",
                regression_check=lambda side: self.fail("matching version must not run post-flash checks"),
            )

            result = updater.upgrade("left", image)

            self.assertTrue(result.skipped)
            self.assertEqual(result.skip_reason, "version_match")
            self.assertEqual(result.command, ())
            self.assertEqual(result.previous_version, result.installed_version)
            self.assertEqual(len(commands), 1)
            complete = [event for event in events if event.get("state") == "complete"]
            self.assertIn("跳过", str(complete[-1]["message"]))

    def test_upgrade_binds_same_port_verifies_version_and_runs_regression(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "camera.img"
            image.write_bytes(b"new firmware")
            reader = executable(root / "linux_read_version")
            trigger = executable(root / "trigger")
            utility = executable(root / "dfu-util")
            commands: list[list[str]] = []
            events: list[dict[str, object]] = []
            versions = iter(("2026-09-01-1220", "2026-09-02-0001"))

            def runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                commands.append(command)
                if command[0] == str(reader.resolve()):
                    return subprocess.CompletedProcess(
                        command,
                        0,
                        "待升级固件版本: 2026-09-02-0001\n"
                        "当前 UVC 摄像头版本 (/dev/cam_left): 2026-09-01-1220\n",
                        "",
                    )
                return subprocess.CompletedProcess(command, 0, "Done!\n", "")

            updater = dfu.MainCameraDfuUpdater(
                firmware_dir=root,
                version_reader=reader,
                dfu_trigger=trigger,
                dfu_util=utility,
                runner=runner,
                event_sink=events.append,
                locate_uvc_port=lambda side: "9-1.2.1",
                enumerate_dfu=lambda: [dfu.DfuDevice("9-1.2.1", alt=0)],
                camera_present=lambda side: True,
                read_device_version=lambda side: next(versions),
                regression_check=lambda side: {"ok": True, "sn": "CAM123"},
            )

            result = updater.upgrade("left", image)

            self.assertEqual(result.usb_port, "9-1.2.1")
            self.assertEqual(result.previous_version, "2026-09-01-1220")
            self.assertEqual(result.installed_version, "2026-09-02-0001")
            flash = next(command for command in commands if command[0] == str(utility.resolve()))
            self.assertEqual(flash[1:], ["-d", "0955:701a", "-p", "9-1.2.1", "-a", "0", "-D", str(image), "-R"])
            states = [event["state"] for event in events if event["type"] == "state"]
            self.assertEqual(states[-1], "complete")
            self.assertIn("waiting_for_dfu", states)
            self.assertIn("regression_check", states)

    def test_dfu_timeout_is_structured_and_emits_failed_state(self) -> None:
        now = [0.0]
        events: list[dict[str, object]] = []

        updater = dfu.MainCameraDfuUpdater(
            event_sink=events.append,
            clock=lambda: now[0],
            sleeper=lambda seconds: now.__setitem__(0, now[0] + seconds),
            enumerate_dfu=lambda: [],
        )
        with self.assertRaises(dfu.DfuError) as raised:
            updater._wait_for_dfu("9-1.2.1", 0.5)
        self.assertEqual(raised.exception.code, "dfu_enumeration_timeout")

    def test_missing_trigger_is_reported_before_any_flash(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "camera.img"
            image.write_bytes(b"firmware")
            reader = executable(root / "linux_read_version")
            commands: list[list[str]] = []
            def runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                commands.append(command)
                return subprocess.CompletedProcess(
                    command, 0, "待升级固件版本: 2026-09-02-0001\n", ""
                )

            updater = dfu.MainCameraDfuUpdater(
                firmware_dir=root,
                version_reader=reader,
                dfu_trigger=root / "missing-trigger",
                runner=runner,
                read_device_version=lambda side: "2026-09-01-1220",
            )
            with self.assertRaises(dfu.PrerequisiteError) as raised:
                updater.upgrade("left", image)
            self.assertEqual(raised.exception.code, "dfu_trigger_missing")
            self.assertEqual(commands, [[str(reader.resolve()), "--image", str(image), "--device", "/dev/cam_left"]])

    def test_version_mismatch_stops_before_regression_check(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "camera.img"
            image.write_bytes(b"new firmware")
            reader = executable(root / "linux_read_version")
            trigger = executable(root / "trigger")
            utility = executable(root / "dfu-util")
            versions = iter(("2026-09-01-1220", "2026-09-01-1220"))
            regression_calls: list[str] = []

            def runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                if command[0] == str(reader.resolve()):
                    return subprocess.CompletedProcess(
                        command, 0, "待升级固件版本: 2026-09-02-0001\n", ""
                    )
                return subprocess.CompletedProcess(command, 0, "Done!\n", "")

            updater = dfu.MainCameraDfuUpdater(
                firmware_dir=root,
                version_reader=reader,
                dfu_trigger=trigger,
                dfu_util=utility,
                runner=runner,
                locate_uvc_port=lambda side: "9-1.2.1",
                enumerate_dfu=lambda: [dfu.DfuDevice("9-1.2.1", alt=0)],
                camera_present=lambda side: True,
                read_device_version=lambda side: next(versions),
                regression_check=lambda side: regression_calls.append(side) or {"ok": True},
            )

            with self.assertRaises(dfu.DfuError) as raised:
                updater.upgrade("left", image)
            self.assertEqual(raised.exception.code, "firmware_version_mismatch")
            self.assertEqual(regression_calls, [])

    def test_image_change_after_inspection_is_rejected_before_entering_dfu(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "camera.img"
            image.write_bytes(b"original")
            reader = executable(root / "linux_read_version")
            trigger = executable(root / "trigger")
            utility = executable(root / "dfu-util")
            commands: list[list[str]] = []

            def runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                commands.append(command)
                if command[0] == str(reader.resolve()):
                    return subprocess.CompletedProcess(command, 0, "待升级固件版本: 2026-09-02-0001\n", "")
                return subprocess.CompletedProcess(command, 0, "Done!\n", "")

            def locate_uvc_port(side: str) -> str:
                image.write_bytes(b"changed")
                return "9-1.2.1"

            updater = dfu.MainCameraDfuUpdater(
                firmware_dir=root,
                version_reader=reader,
                dfu_trigger=trigger,
                dfu_util=utility,
                runner=runner,
                locate_uvc_port=locate_uvc_port,
                enumerate_dfu=lambda: [dfu.DfuDevice("9-1.2.1", alt=0)],
                read_device_version=lambda side: "2026-09-01-1220",
                regression_check=lambda side: {"ok": True},
            )
            with self.assertRaises(dfu.DfuError) as raised:
                updater.upgrade("left", image)
            self.assertEqual(raised.exception.code, "firmware_changed_before_dfu")
            self.assertFalse(any(command[0] == str(trigger.resolve()) for command in commands))
            self.assertFalse(any(command[0] == str(utility.resolve()) for command in commands))

    def test_confirmation_snapshot_change_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "camera.img"
            image.write_bytes(b"new firmware")
            reader = executable(root / "linux_read_version")
            preview = dfu.FirmwareImage(
                path=str(image),
                name=image.name,
                size=image.stat().st_size,
                sha256="0" * 64,
                version="2026-09-02-0001",
            )

            def runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                return subprocess.CompletedProcess(
                    command, 0, "待升级固件版本: 2026-09-02-0001\n", ""
                )

            updater = dfu.MainCameraDfuUpdater(
                firmware_dir=root,
                version_reader=reader,
                runner=runner,
                read_device_version=lambda side: self.fail("snapshot mismatch must stop first"),
            )
            with self.assertRaises(dfu.DfuError) as raised:
                updater.upgrade("left", image, expected_image=preview)
            self.assertEqual(raised.exception.code, "firmware_changed_after_confirmation")

    def test_zero_exit_without_completion_marker_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "camera.img"
            image.write_bytes(b"new firmware")
            reader = executable(root / "linux_read_version")
            trigger = executable(root / "trigger")
            utility = executable(root / "dfu-util")

            def runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                if command[0] == str(reader.resolve()):
                    return subprocess.CompletedProcess(
                        command, 0, "待升级固件版本: 2026-09-02-0001\n", ""
                    )
                if command[0] == str(utility.resolve()):
                    return subprocess.CompletedProcess(command, 0, "No error reported\n", "")
                return subprocess.CompletedProcess(command, 0, "triggered\n", "")

            updater = dfu.MainCameraDfuUpdater(
                firmware_dir=root,
                version_reader=reader,
                dfu_trigger=trigger,
                dfu_util=utility,
                runner=runner,
                locate_uvc_port=lambda side: "9-1.2.1",
                enumerate_dfu=lambda: [dfu.DfuDevice("9-1.2.1", alt=0)],
                read_device_version=lambda side: "2026-09-01-1220",
                regression_check=lambda side: {"ok": True},
            )

            with self.assertRaises(dfu.DfuError) as raised:
                updater.upgrade("left", image)
            self.assertEqual(raised.exception.code, "dfu_flash_completion_missing")

    def test_dfu_util_reset_error_after_download_uses_uvc_version_as_truth(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            image = root / "camera.img"
            image.write_bytes(b"new firmware")
            reader = executable(root / "linux_read_version")
            trigger = executable(root / "trigger")
            utility = executable(root / "dfu-util")
            versions = iter(("2026-09-01-1220", "2026-09-03-1824"))
            events: list[dict[str, object]] = []

            def runner(command: list[str], **_: object) -> subprocess.CompletedProcess[str]:
                if command[0] == str(reader.resolve()):
                    return subprocess.CompletedProcess(
                        command, 0, "待升级固件版本: 2026-09-03-1824\n", ""
                    )
                if command[0] == str(utility.resolve()):
                    return subprocess.CompletedProcess(
                        command,
                        74,
                        "Download done.\nResetting USB to switch back to runtime mode\n",
                        "dfu-util: error resetting after download\n",
                    )
                return subprocess.CompletedProcess(command, 0, "triggered\n", "")

            updater = dfu.MainCameraDfuUpdater(
                firmware_dir=root,
                version_reader=reader,
                dfu_trigger=trigger,
                dfu_util=utility,
                runner=runner,
                event_sink=events.append,
                locate_uvc_port=lambda side: "9-1.2.1",
                enumerate_dfu=lambda: [dfu.DfuDevice("9-1.2.1", alt=0)],
                camera_present=lambda side: True,
                read_device_version=lambda side: next(versions),
                regression_check=lambda side: {"ok": True},
            )

            result = updater.upgrade("left", image)

            self.assertEqual(result.installed_version, "2026-09-03-1824")
            warnings = [
                event for event in events
                if event.get("level") == "warning" and event.get("stream") == "app"
            ]
            self.assertTrue(warnings)


if __name__ == "__main__":
    unittest.main()
