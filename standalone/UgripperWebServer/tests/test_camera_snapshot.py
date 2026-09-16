import sys
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import camera_snapshot as snap


class SnapshotTest(unittest.TestCase):
    def test_capture_uses_sensor_parameters_and_output_transform(self):
        camera = dict(mode='hybrid-decode-encode', device='/dev/tcam_left_l',
                      width=640, height=480, capture_width=1280, capture_height=960,
                      fps=120, video_filter='hflip')
        args = snap.capture_args(camera, {}, Path('/tmp/frame.jpg'))
        self.assertEqual(args[args.index('-video_size') + 1], '1280x960')
        self.assertEqual(args[args.index('-framerate') + 1], '120')
        self.assertEqual(args[args.index('-input_format') + 1], 'mjpeg')
        self.assertEqual(args[args.index('-vf') + 1], 'scale=640:480:flags=neighbor,hflip')

    def test_main_codec_follows_setting(self):
        camera = dict(mode='direct-copy-h265', device='/dev/cam_left', width=1920, height=1080, fps=60)
        for codec, expected in [('h264', 'h264'), ('h265', 'hevc')]:
            args = snap.capture_args(camera, {'CAMERA_CODEC': codec}, Path('/tmp/frame.jpg'))
            self.assertEqual(args[args.index('-input_format') + 1], expected)

    def fake_ego(self, fail_pull=False):
        adb = Mock(serial='test-ego')
        def run(*args, **kwargs):
            if args[0] == 'pull':
                if fail_pull:
                    raise OSError('pull failed')
                Path(args[2]).write_bytes(b'video')
            return SimpleNamespace(returncode=0)
        adb.run.side_effect = run
        ego = Mock(PACKAGE='test.app')
        ego.detect_egos.return_value = ([adb], [])
        ego.snapshot_temp_episodes.return_value = {'/dataset': set()}
        ego.find_new_temp_episodes.return_value = [('/dataset', 'episode_test-temp')]
        ego.final_name.return_value = 'episode_test'
        ego.remote_dir_exists.return_value = True
        ego.remote_path.side_effect = lambda r, e, f: r + '/' + e + '/' + f
        ego.remote_file_size.return_value = 5
        ego.remove_remote_episode.return_value = (True, '')
        return ego, adb

    def test_ego_success_deletes_only_created_episode_after_all_images(self):
        ego, adb = self.fake_ego()
        def image(args, *unused):
            Path(args[-1]).write_bytes(b'jpeg')
        with tempfile.TemporaryDirectory() as tmp, patch.object(snap, 'ego_module', return_value=ego), \
             patch.object(snap, 'read_json', return_value={}), patch.object(snap, 'read_settings', return_value={}), \
             patch.object(snap.time, 'sleep'), patch.object(snap, 'checked', side_effect=image):
            result = snap.capture_ego(Path(tmp))
            self.assertEqual(len(result), 3)
            ego.remove_remote_episode.assert_called_once_with(adb, '/dataset', 'episode_test')
            self.assertFalse((Path(tmp) / 'ego_video').exists())

    def test_ego_failed_pull_keeps_remote_data_and_stops_recording(self):
        ego, adb = self.fake_ego(fail_pull=True)
        with tempfile.TemporaryDirectory() as tmp, patch.object(snap, 'ego_module', return_value=ego), \
             patch.object(snap, 'read_json', return_value={}), patch.object(snap, 'read_settings', return_value={}), \
             patch.object(snap.time, 'sleep'):
            with self.assertRaises(OSError):
                snap.capture_ego(Path(tmp))
        ego.remove_remote_episode.assert_not_called()
        self.assertTrue(any('test.app.STOP_RECORDING' in call.args for call in adb.run.call_args_list))

    def test_ego_existing_recording_is_untouched(self):
        ego, adb = self.fake_ego()
        ego.snapshot_temp_episodes.return_value = {'/dataset': {'episode_existing-temp'}}
        with tempfile.TemporaryDirectory() as tmp, patch.object(snap, 'ego_module', return_value=ego), \
             patch.object(snap, 'read_json', return_value={}):
            with self.assertRaises(ValueError):
                snap.capture_ego(Path(tmp))
        adb.run.assert_not_called()
        ego.remove_remote_episode.assert_not_called()


if __name__ == '__main__':
    unittest.main()
