#!/usr/bin/env python3
"""Exercise real media/MCAP inputs and the time-domain boundaries in the skill."""
import importlib.util
import json
import sys
import tempfile
import unittest
import wave
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('episode_timing_validator', ROOT / '.codex/skills/validate-episode-data/scripts/validate_episode.py')
V = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = V
SPEC.loader.exec_module(V)
BASE = 1_790_000_000_000_000


def write_mcap(path, records):
    from mcap.writer import Writer
    with path.open('wb') as stream:
        writer = Writer(stream)
        writer.start()
        channels = {}
        for topic, log, publish, payload in records:
            if topic not in channels:
                channels[topic] = writer.register_channel(topic=topic, message_encoding='binary', schema_id=0)
            writer.add_message(channel_id=channels[topic], log_time=log, publish_time=publish, data=payload)
        writer.finish()


class AllModalityTimingTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='episode-timing-')
        self.addCleanup(self.tmp.cleanup)
        self.episode = Path(self.tmp.name) / 'episode_20261015_0001'
        self.episode.mkdir()
        self.v = V.EpisodeValidator(V.build_parser().parse_args([str(self.episode)]))

    def test_file_order_rollbacks_and_subthreshold_gaps_remain_visible(self):
        gap = V.summarize_timing_gaps([0, 1, 20_000_001, 10_000_001], 30, 3)
        self.assertEqual(gap['backward_count'], 1)
        self.assertEqual(gap['one_ns_count'], 1)
        self.assertEqual(gap['max_gap_ms'], 20)
        self.assertEqual(gap['large_gap_count'], 0)
        self.assertIsNone(V.summarize_timing_gaps([0], 30, 3)['large_gap_count'])

    def test_real_media_and_mcap_offsets_and_all_gap_bases(self):
        native = self.episode / 'cam_left.mkv'
        result = V.run_command(['ffmpeg', '-v', 'error', '-f', 'lavfi', '-i', 'color=size=16x16:rate=25',
                                '-frames:v', '4', '-vf', "setpts='PTS+gte(N,2)*0.12/TB'",
                                '-vsync', 'vfr', '-c:v', 'ffv1', str(native)])
        self.assertEqual(result.returncode, 0, result.stderr)
        ego = self.episode / 'ego';ego.mkdir()
        (ego / 'rgb.mkv').write_bytes(native.read_bytes())
        with wave.open(str(ego / 'audio.wav'), 'wb') as audio:
            audio.setnchannels(1);audio.setsampwidth(2);audio.setframerate(8000)
            audio.writeframes(b'\x00\x00' * 8000)
        self.v.active_video_files = [('left_cam_main', 'cam_left.mkv', 'left')]
        self.v.video_start_offsets_us = {'cam_left.mkv': BASE}
        self.v.ego_metadata = {'video_details': [{'name': 'rgb.mkv', 'start_offset_us': BASE+150_000}],
                               'head_pose_details': [{'name': 'head_pose', 'start_offset_us': BASE+100_000}]}
        write_mcap(self.episode / 'sensor_left.mcap',
                   [('encoder_left', (BASE+t)*1000, (BASE+t)*1000, b'x') for t in (0, 1000, 21000, 21001)])
        write_mcap(ego / 'sensor.mcap',
                   [('head_pose', t, t, b'x') for t in (5_000_000_000, 5_005_000_000, 5_010_000_000)])
        write_mcap(self.episode / 'extra.mcap', [('pressure', (BASE+t)*1000, (BASE+t)*1000, b'x') for t in (0, 1000, 2000)])
        write_mcap(self.episode / 'fays_data_left.mcap',
                   [('i', t*1_000_000, (BASE+t*1000)*1000, b'\0'*48) for t in (0, 1, 2)] +
                   [('c', t*1_000_000, (BASE+t*1000)*1000, n.to_bytes(4,'little')) for n,t in enumerate((0, 40, 160))])
        self.v.scan_videos();self.v.scan_additional_media();self.v.scan_sensors();self.v.scan_fays_mcaps()
        self.v.build_modality_timing()
        table = self.v.modality_timing;rows = {r['name']:r for r in table['streams']}
        self.assertEqual(rows['video:ego/rgb.mkv']['start_offset_ms'], 150)
        self.assertEqual(rows['video:cam_left.mkv']['end_offset_ms'], -150)
        self.assertEqual(rows['sensor:ego:head_pose']['start_offset_ms'], 100)
        self.assertEqual(rows['sensor:ego:head_pose']['end_unix_us'], BASE+110_000)
        self.assertEqual(rows['sensor:encoder_left']['gaps'][0]['max_gap_ms'], 20)
        self.assertEqual(rows['sensor:encoder_left']['gaps'][0]['large_gap_count'], 0)
        self.assertIn('sensor:extra.mcap:pressure', rows)
        self.assertEqual(rows['video:cam_left.mkv']['gaps'][0]['large_gap_count'], 1)
        self.assertEqual(rows['video:ego/rgb.mkv']['gaps'][0]['large_gap_count'], 1)
        self.assertIsNone(rows['audio:ego/audio.wav']['start_offset_ms'])
        self.assertIsNotNone(rows['audio:ego/audio.wav']['gaps'][0]['max_gap_ms'])
        camera = rows['fays:left:camera']
        self.assertEqual({g['basis'] for g in camera['gaps']}, {'device/log_time','publish_time'})
        self.assertEqual(camera['gaps'][0]['max_gap_ms'], 120)
        self.assertEqual(camera['gaps'][0]['large_gap_count'], 1)
        self.assertIsNone(rows['fays:right:camera']['start_offset_ms'])
        self.assertIsNone(rows['sensor:encoder_right']['end_offset_ms'])

    def test_cli_keeps_missing_modalities_visible_in_json_and_text(self):
        args = [sys.executable, str(Path(V.__file__)), str(self.episode)]
        result = V.run_command(args + ['--json'])
        self.assertEqual(result.returncode, 2, result.stderr)
        payload = json.loads(result.stdout)
        rows = {r['name']: r for r in payload['summary']['modality_timing']['streams']}
        self.assertIsNone(rows['video:cam_left.mkv']['start_offset_ms'])
        self.assertIsNone(rows['video:cam_left.mkv']['gaps'][0]['large_gap_count'])
        self.assertIn('global_first_frame_sync', payload['summary'])
        self.assertTrue(payload['findings'])
        text = V.run_command(args)
        self.assertEqual(text.returncode, 2, text.stderr)
        line = next(line for line in text.stdout.splitlines() if line.startswith('| video:cam_left.mkv |'))
        self.assertIn('N/A', line)
        self.assertIn('All-Modality Gap Summary', text.stdout)

    def test_packet_scan_skip_is_not_reported_as_zero_gaps(self):
        self.v.active_video_files = [('left_cam_main', 'cam_left.mkv', 'left')]
        self.v.args.skip_video_packets = True
        self.v.video_stats['left_cam_main'] = V.VideoStats(name='left_cam_main',side='left',path='cam_left.mkv',
                    first_system_time_us=BASE,last_system_time_us=BASE+1_000_000)
        self.v.build_modality_timing()
        row = next(r for r in self.v.modality_timing['streams'] if r['kind']=='video')
        self.assertFalse(row['gaps'][0]['scanned'])
        self.assertIsNone(row['gaps'][0]['large_gap_count'])
        self.assertIn('estimate', row['alignment_method'])


if __name__ == '__main__':
    unittest.main()
