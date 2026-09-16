"""On-demand images. Callers hold operation_lock for the entire capture."""
import concurrent.futures
import importlib.util
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import stat
import time

from common import ROOT, STATE, atomic_json, command, read_json
from environment_config import read_settings

SNAPSHOTS = STATE / 'snapshots'


def checked(argv, timeout=20):
    argv = list(argv)
    if argv[0] == 'ffmpeg' and Path('/usr/local/bin/ffmpeg').is_file():
        argv[0] = '/usr/local/bin/ffmpeg'
    result = command(argv, timeout)
    if result.returncode:
        raise ValueError((result.stderr or result.stdout)[-1500:] or '抓图命令失败')
    return result


def camera_configs():
    path = ROOT / 'bin/CameraRecorder/config/camera_recorder.yaml'
    try:
        import yaml
    except ModuleNotFoundError:
        # The bundled recording venv has no PyYAML; the board's OS supplies it.
        # Use that interpreter without adding OS packages to the venv sys.path.
        parsed = checked(['/usr/bin/python3', '-c',
                          'import json,sys,yaml; print(json.dumps(yaml.safe_load(open(sys.argv[1]))))',
                          str(path)])
        data = json.loads(parsed.stdout)
    else:
        data = yaml.safe_load(path.read_text())
    if 'CameraRecorder' in data:
        result = []
        for c in data['CameraRecorder']['cameras']:
            d, r = c['device'], c['record']
            result.append(dict(d, **r, name=c['id'], device=d['path'], mode=r['pipeline']))
        return result
    return data['cameras']


def capture_args(c, settings, output):
    main = c['mode'] == 'direct-copy-h265'
    codec = settings.get('CAMERA_CODEC') or 'h265'
    if codec not in ('h264', 'h265'):
        raise ValueError('CAMERA_CODEC 无效')
    fmt = ('h264' if codec == 'h264' else 'hevc') if main else c.get('input_format') or 'mjpeg'
    w, h = int(c.get('capture_width') or c['width']), int(c.get('capture_height') or c['height'])
    if min(w, h, int(c['fps'])) <= 0:
        raise ValueError('相机采集参数无效')
    args = ['ffmpeg', '-hide_banner', '-loglevel', 'error', '-nostdin', '-y',
            '-f', 'v4l2', '-input_format', fmt, '-framerate', str(c['fps']),
            '-video_size', f'{w}x{h}', '-i', c['device']]
    filters = []
    if (w, h) != (int(c['width']), int(c['height'])):
        filters.append(f"scale={int(c['width'])}:{int(c['height'])}:flags=neighbor")
    if c.get('video_filter'):
        filters.append(c['video_filter'])
    if filters:
        args += ['-vf', ','.join(filters)]
    return args + ['-frames:v', '1', '-threads', '1', '-update', '1', str(output)]


def capture_camera(c, settings, output, lossless=False):
    name = c['name']
    image_name = name + ('.png' if lossless else '.jpg')
    result = dict(camera=name, requested_at_ns=time.time_ns(), parameters=c)
    try:
        if not re.fullmatch(r'[A-Za-z0-9_-]+', name):
            raise ValueError('相机名称无效')
        if not Path(c['device']).exists():
            raise ValueError('相机未连接')
        if c['mode'] == 'direct-copy-h265':
            # Match the recorder: apply the mains-frequency control if supported.
            controls = checked(['v4l2-ctl', '-d', c['device'], '--list-ctrls'])
            if 'power_line_frequency' in controls.stdout:
                frequency = settings.get('UGRIPPER_POWER_LINE_FREQUENCY') or '50'
                if frequency not in ('50', '60'):
                    raise ValueError('电源频率配置无效')
                checked(['v4l2-ctl', '-d', c['device'], '--set-ctrl',
                         'power_line_frequency=' + ('2' if frequency == '60' else '1')])
        if c['mode'] == 'direct-copy-h265' and (settings.get('CAMERA_CODEC') or 'h265') == 'h265':
            # Match the H.265 recording input path; older FFmpeg V4L2 builds
            # cannot negotiate HEVC, so collect a short encoded stream with GST.
            clip = output / (name + '.mkv')
            try:
                checked(['gst-launch-1.0', '-q', '-e', 'v4l2src',
                         'device=' + c['device'], 'num-buffers=' + str(c['fps']), '!',
                         f"video/x-h265,width={c['width']},height={c['height']},framerate={c['fps']}/1",
                         '!', 'h265parse', '!', 'matroskamux', '!', 'filesink', 'location=' + str(clip)], 20)
                checked(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-nostdin', '-y',
                         '-i', str(clip), '-frames:v', '1', '-threads', '1', '-update', '1',
                         str(output / image_name)])
            finally:
                clip.unlink(missing_ok=True)
        else:
            checked(capture_args(c, settings, output / image_name), 20)
        if not (output / image_name).is_file():
            raise ValueError('未获取到可解码的帧')
        result.update(status='ok', image=image_name, completed_at_ns=time.time_ns())
    except Exception as exc:
        result.update(status='failed', error=str(exc))
    return result


def capture_stereo(side, output):
    result = dict(camera=f'stereo_{side}', requested_at_ns=time.time_ns())
    token = secrets.token_hex(16)
    base = Path('/dev/shm/ugripper') / ('getimg_' + token)
    try:
        fifo = Path(f'/dev/shm/ugripper/umi_{side}_fays_cmd')
        fd = os.open(fifo, os.O_WRONLY | os.O_NONBLOCK | os.O_NOFOLLOW)
        try:
            if not stat.S_ISFIFO(os.fstat(fd).st_mode):
                raise ValueError('双目控制入口不是 FIFO')
            os.write(fd, f'GETIMG|{token}\n'.encode())
        finally:
            os.close(fd)
        deadline = time.monotonic() + 5
        while not base.with_suffix('.json').exists():
            if time.monotonic() >= deadline:
                raise ValueError('双目取帧超时，请检查预热状态及 recorder 版本')
            time.sleep(.05)
        name = f'stereo_{side}.jpg'
        checked(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-nostdin', '-y',
                 '-i', str(base.with_suffix('.pnm')), '-frames:v', '1', '-threads', '1',
                 '-update', '1', str(output / name)])
        result.update(read_json(base.with_suffix('.json')))
        result.update(status='ok', image=name)
    except Exception as exc:
        result.update(status='failed', error=str(exc))
    finally:
        for suffix in ('.pnm', '.json', '.pnm.tmp', '.json.tmp'):
            base.with_suffix(suffix).unlink(missing_ok=True)
    return result


def capture_local(output):
    settings = read_settings()
    configs = [c for c in camera_configs() if
               c['name'] != 'chest_cam_main' or settings.get('ENABLE_CHEST_CAM_MAIN') == 'true']
    # Each device is independent. No recorder or prewarm process is stopped.
    with concurrent.futures.ThreadPoolExecutor(max_workers=9) as pool:
        futures = [pool.submit(capture_camera, c, settings, output) for c in configs]
        for side in ('left', 'right'):
            futures.append(pool.submit(capture_stereo, side, output))
        return [f.result() for f in futures]


def ego_module():
    path = ROOT / 'bin/UgripperRuntime/ego/ego_recording_worker.py'
    spec = importlib.util.spec_from_file_location('ego_snapshot_worker', path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def capture_ego(output):
    ego = ego_module()
    binding = read_json('/dev/shm/ugripper/ego_binding.json')
    devices, _ = ego.detect_egos(str(binding.get('serial') or ''))
    if len(devices) != 1:
        raise ValueError('请连接一台 Ego 或先绑定指定 Ego')
    adb = devices[0]
    previous = ego.snapshot_temp_episodes(adb)
    if any(previous.values()):
        raise ValueError('Ego 存在未结束录制，暂不能短录抓图')
    root = episode = ''
    # START can have reached the device even if ADB times out: always attempt STOP.
    try:
        ego.set_ego_video_codec(adb, read_settings().get('CAMERA_CODEC') or 'h265')
        adb.run('shell', 'am', 'broadcast', '-a', f'{ego.PACKAGE}.START_RECORDING', check=True)
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            candidates = ego.find_new_temp_episodes(adb, previous)
            if len(candidates) > 1:
                raise ValueError('出现多条新 Ego 数据，无法确定测试数据')
            if candidates:
                root, episode = candidates[0]
                atomic_json(output / 'ego_test.json', dict(serial=adb.serial, remote_root=root,
                            remote_episode=episode, cleanup='pending'))
                break
            time.sleep(.2)
        if not episode:
            raise ValueError('Ego 未创建测试录制')
        time.sleep(3)
    finally:
        stopped = False
        for attempt in range(3):
            try:
                adb.run('shell', 'am', 'broadcast', '-a', f'{ego.PACKAGE}.STOP_RECORDING', check=True)
                stopped = True
                break
            except Exception:
                time.sleep(.1)
        if not stopped:
            raise ValueError('Ego 停录失败，请检查设备；测试数据未删除')
    final = ego.final_name(episode)
    deadline = time.monotonic() + 60
    while not ego.remote_dir_exists(adb, root, final):
        if time.monotonic() >= deadline:
            raise ValueError('Ego 测试录制收尾超时；数据未删除')
        time.sleep(.2)
    videos = output / 'ego_video'
    videos.mkdir()
    images = []
    for camera in ('rgb', 'ctrl', 'tracking'):
        remote = ego.remote_path(root, final, camera + '.mp4')
        local = videos / (camera + '.mp4')
        size = ego.remote_file_size(adb, remote)
        if not size:
            raise ValueError(f'Ego {camera} 视频缺失；数据未删除')
        adb.run('pull', remote, str(local), timeout=60, check=True)
        if local.stat().st_size != size:
            raise ValueError(f'Ego {camera} 拉取不完整；数据未删除')
        ego.repair_mp4_mdat_largesize(local)
        name = 'ego_' + camera + '.jpg'
        checked(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-nostdin', '-y',
                 '-ss', '1', '-i', str(local), '-frames:v', '1', '-threads', '1',
                 '-update', '1', str(output / name)])
        if not (output / name).is_file() or not (output / name).stat().st_size:
            raise ValueError(f'Ego {camera} 未截取到图片；数据未删除')
        images.append(dict(camera='ego_' + camera, status='ok', image=name, video_position_s=1))
    removed, error = ego.remove_remote_episode(adb, root, final)
    atomic_json(output / 'ego_test.json', dict(serial=adb.serial, remote_root=root,
                remote_episode=final, cleanup='deleted' if removed else 'failed', error=error))
    if not removed:
        images.append(dict(camera='ego_cleanup', status='failed', error=error))
    else:
        shutil.rmtree(videos)
    return images


def capture(kind, output):
    output = Path(output)
    output.mkdir(parents=True, exist_ok=True)
    result = dict(kind=kind, status='running', started_at_ns=time.time_ns(), cameras=[])
    atomic_json(output / 'result.json', result)
    try:
        if kind == 'tactile':
            from tactile_panel import run_detection
            result['cameras'] = run_detection(output)
        else:
            result['cameras'] = capture_ego(output) if kind == 'ego' else capture_local(output)
        result['status'] = 'ok' if all(c['status'] == 'ok' for c in result['cameras']) else 'partial'
    except Exception as exc:
        result.update(status='failed', error=str(exc))
    shutil.rmtree(output / 'ego_video', ignore_errors=True)
    result['completed_at_ns'] = time.time_ns()
    atomic_json(output / 'result.json', result)
    return result


def submit_snapshot(kind):
    import threading
    from common import operation_lock
    if kind not in ('local', 'ego', 'tactile'):
        raise ValueError('未知抓图类型')
    stack = operation_lock()
    stack.__enter__()
    try:
        SNAPSHOTS.mkdir(parents=True, exist_ok=True)
        # Keep only one completed capture, with a unique URL per request.
        for old in SNAPSHOTS.iterdir():
            if old.is_dir() and re.fullmatch(r'[0-9a-f]{32}', old.name):
                shutil.rmtree(old)
        job = secrets.token_hex(16)
        output = SNAPSHOTS / job
        output.mkdir()
        atomic_json(output / 'result.json', dict(status='running', cameras=[]))
        def work():
            try:
                checked(['/usr/bin/python3' if kind == 'tactile' else str(ROOT / '.venv/bin/python3'), str(Path(__file__).resolve()),
                         '--capture', kind, str(output)], 300)
            except Exception as exc:
                atomic_json(output / 'result.json', dict(status='failed', error=str(exc), cameras=[]))
            finally:
                shutil.rmtree(output / 'ego_video', ignore_errors=True)
                stack.__exit__(None, None, None)
        threading.Thread(target=work, daemon=False).start()
        return dict(id=job)
    except Exception:
        stack.__exit__(None, None, None)
        raise


if __name__ == '__main__':
    import sys
    if len(sys.argv) != 4 or sys.argv[1] != '--capture' or sys.argv[2] not in ('local', 'ego', 'tactile'):
        raise SystemExit('internal capture arguments required')
    capture(sys.argv[2], Path(sys.argv[3]))
