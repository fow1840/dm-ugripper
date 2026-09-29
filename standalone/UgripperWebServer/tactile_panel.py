"""Manual cover inspection; baseline mutations are serialized by the runtime."""
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import tempfile
import time

from common import ROOT, atomic_json, read_json, command
from camera_snapshot import capture_camera, camera_configs, checked
from environment_config import read_settings

TACTILE_STATE = Path('/var/lib/ugripper/tactile_state')
CAMERAS = {f'{side}_tcam_{slot}': dict(side=side, slot=slot,
           label=('左夹爪' if side == 'left' else '右夹爪') + ' · ' + ('左' if slot == 'l' else '右') + '触觉传感器',
           device=f'/dev/tcam_{side}_{slot}') for side in ('left', 'right') for slot in ('l', 'r')}


def sensor_serial(device):
    node = Path(device).resolve(strict=True).name
    parent = (Path('/sys/class/video4linux') / node / 'device').resolve(strict=True)
    for directory in (parent, *parent.parents):
        path = directory / 'serial'
        if path.is_file():
            serial = path.read_text().strip()
            if serial:
                return serial
    raise ValueError('无法读取触觉传感器序列号')


def baseline_path(scope, serial):
    return TACTILE_STATE / scope / (re.sub(r'[^A-Za-z0-9_.-]', '_', serial) + '.gray')


def panel_status():
    history = read_json(TACTILE_STATE / 'history.json').get('cameras', {})
    result = []
    for name, identity in CAMERAS.items():
        item = dict(camera=name, **identity)
        try:
            serial = sensor_serial(identity['device'])
            saved = history.get(serial, {})
            recent = saved.get('recent', [])
            item.update(serial=serial, warning=bool(saved.get('persistent_fault_active')) or
                        (len(recent) >= 3 and all(v is True for v in recent)),
                        reference=baseline_path('reference', serial).is_file(),
                        persistent=baseline_path('persistent', serial).is_file())
        except (OSError, ValueError) as exc:
            item.update(error=str(exc), warning=None)
        result.append(item)
    return result


def run_detection(output):
    configs = {c['name']: c for c in camera_configs()}
    results = []
    for state in panel_status():
        name = state['camera']
        result = dict(state, status='failed')
        results.append(result)
        try:
            if 'error' in state:
                raise ValueError(state['error'])
            capture = capture_camera(configs[name], read_settings(), output, lossless=True)
            if capture['status'] != 'ok':
                raise ValueError(capture['error'])
            if sensor_serial(state['device']) != state['serial']:
                raise ValueError('传感器发生更换，请重新检测')
            result.update(image=capture['image'], captured_at_ms=int(time.time()*1000))
            # Use exactly the runtime's FFmpeg preprocessing, not Pillow resampling.
            raw = output / (name + '.gray')
            checked(['ffmpeg', '-hide_banner', '-loglevel', 'error', '-nostdin', '-y',
                     '-i', str(output / capture['image']), '-vf',
                     'crop=iw*0.82:ih:iw*0.18:0,scale=160:120,format=gray',
                     '-frames:v', '1', '-f', 'rawvideo', str(raw)])
            if raw.stat().st_size != 160 * 120:
                raise ValueError("触觉预处理帧尺寸无效")
            result['comparisons'] = []
            for scope in ('reference', 'persistent'):
                item = dict(scope=scope)
                result['comparisons'].append(item)
                path = baseline_path(scope, state['serial'])
                if not path.exists():
                    item.update(status='missing', detail='尚未建立基线')
                    continue
                # Use the same deployed CLI and exit-code contract as board checks.
                with tempfile.TemporaryDirectory(prefix='.detect-', dir=output) as work:
                    completed = command([str(ROOT / 'bin/tactile_damage_detector/detect'),
                                         '--baseline', str(path), '--image', str(raw),
                                         '--preprocessed', '--output-dir', work], 30)
                    if completed.returncode not in (0, 2):
                        raise ValueError((completed.stderr or completed.stdout)[-1500:])
                    detail = read_json(Path(work) / 'result.json')
                    if not isinstance(detail.get('damaged'), bool):
                        raise ValueError('检测器未返回有效结论')
                    comparison = f'{name}_{scope}_comparison.png'
                    shutil.copyfile(Path(work) / 'comparison.png', output / comparison)
                    shutil.copyfile(Path(work) / 'mask.png', output / f'{name}_{scope}_mask.png')
                item.update(status='baseline_mismatch' if not detail.get('baseline_compatible', True)
                            else 'damaged' if detail['damaged'] else 'clean',
                            image=comparison, metrics=detail)
            result['status'] = 'ok'
        except Exception as exc:
            result['error'] = str(exc)
    return results


def reset_baseline(output, camera, scope, serial):
    if camera not in CAMERAS or scope not in ('reference', 'persistent', 'both'):
        raise ValueError('传感器或基线类型无效')
    previous = read_json(output / 'result.json')
    sample = next((c for c in previous.get('cameras', []) if c.get('camera') == camera), {})
    if sample.get('status') != 'ok' or sample.get('serial') != serial:
        raise ValueError('请先检测并确认当前传感器图像')
    if not 0 <= time.time()*1000 - sample.get('captured_at_ms', 0) < 120000:
        raise ValueError('图片已超过两分钟，请重新检测')
    if sensor_serial(CAMERAS[camera]['device']) != serial:
        raise ValueError('传感器已更换，请重新检测')
    raw = (output / (camera + '.gray')).read_bytes()
    if len(raw) != 160 * 120:
        raise ValueError('待重置图像无效')
    token = secrets.token_hex(16)
    TACTILE_STATE.mkdir(parents=True, exist_ok=True)
    frame = TACTILE_STATE / 'web_reset.gray'
    request = TACTILE_STATE / 'web_reset_request.json'
    response = TACTILE_STATE / 'web_reset_response.json'
    try:
        response.unlink(missing_ok=True)
        frame.write_bytes(raw)
        # Runtime runs as ubuntu; reset work products must not change state ownership.
        import pwd
        owner = pwd.getpwnam('ubuntu')
        os.chown(frame, owner.pw_uid, owner.pw_gid)
        atomic_json(request, dict(id=token, camera=camera, serial=serial, scope=scope,
                                 requested_at_ms=int(time.time()*1000)))
        deadline = time.monotonic() + 18
        while time.monotonic() < deadline:
            result = read_json(response)
            if result.get('id') == token:
                if result.get('status') != 'ok':
                    raise ValueError(result.get('error', '基线重置失败'))
                return result
            time.sleep(.1)
        raise ValueError('基线重置未收到确认，请刷新状态后检查')
    finally:
        request.unlink(missing_ok=True)
        frame.unlink(missing_ok=True)
        response.unlink(missing_ok=True)


def latest_alarm(include_masks=False):
    """Single atomic runtime snapshot, independent of manual inspection previews."""
    path = TACTILE_STATE / 'latest_alarm.json'
    try:
        if path.stat().st_size > 512 * 1024:
            raise ValueError('告警图像文件过大')
        value = json.loads(path.read_text())
        if (not isinstance(value, dict) or value.get('width') != 160 or
                value.get('height') != 120 or not isinstance(value.get('id'), str) or
                not isinstance(value.get('images'), list) or len(value['images']) > 8):
            raise ValueError('告警图像格式无效')
        images = []
        for item in value['images']:
            if (not isinstance(item, dict) or item.get('camera') not in CAMERAS or
                    item.get('scope') not in ('reference', 'persistent') or
                    not isinstance(item.get('mask'), str) or len(item['mask']) != 19200 or
                    set(item['mask']) - {'0', '1'}):
                raise ValueError('告警 mask 格式无效')
            frame = item.get('current_gray_hex')
            if not isinstance(frame, str) or len(frame) != 38400 or set(frame) - set('0123456789abcdef'):
                raise ValueError('告警缺少有效盖板底图，等待下一次录制检测更新')
            images.append(dict(item) if include_masks else
                          {k: v for k, v in item.items() if k not in ('mask', 'current_gray_hex')})
        return dict(value, images=images)
    except FileNotFoundError:
        return {}
    except (OSError, ValueError, TypeError) as exc:
        return {'error': str(exc)}


def alarm_mask_png(generation, camera, scope):
    """Overlay the saved decision mask on its exact cover frame, in translucent red."""
    import struct
    import zlib
    saved = latest_alarm(include_masks=True)
    if saved.get('id') != generation or not generation:
        raise ValueError('告警图像已更新，请刷新页面')
    item = next((v for v in saved.get('images', [])
                 if v['camera'] == camera and v['scope'] == scope), None)
    if item is None:
        raise ValueError('没有对应的告警图像')
    frame = bytes.fromhex(item['current_gray_hex'])
    pixels = bytearray()
    for gray, mask in zip(frame, item['mask']):
        if mask == '1':
            pixels.extend(((gray * 45 + 255 * 55 + 50) // 100,
                           (gray * 45 + 50) // 100, (gray * 45 + 50) // 100))
        else:
            pixels.extend((gray, gray, gray))
    scanlines = b''.join(b'\0' + pixels[y*480:(y+1)*480] for y in range(120))
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', 160, 120, 8, 2, 0, 0, 0)) +
            chunk(b'IDAT', zlib.compress(scanlines)) + chunk(b'IEND', b''))
