#!/usr/bin/env python3
"""Black/white presentation with GLFW/OpenGL swap interval 1; no timer fallback."""
import argparse
import math
import statistics
import sys
import time


def schedule(refresh_hz, requested_hz):
    """Prefer integral refresh counts, including 59.94/60 Hz nominal modes."""
    ratio = refresh_hz / requested_hz
    hold = max(1, round(ratio))
    if abs(ratio - hold) <= .002 * hold:
        return refresh_hz / hold, hold
    if requested_hz > refresh_hz:
        raise ValueError(f'屏幕 {refresh_hz:g} Hz 无法每秒显示 {requested_hz} 次颜色切换')
    return float(requested_hz), None


def white_frame(frame, refresh_hz, switch_hz, hold):
    index = frame // hold if hold is not None else math.floor(frame * switch_hz / refresh_hz + 1e-9)
    return index % 2 == 0


def check_pacing(intervals, refresh_hz):
    """Reject swaps that are unthrottled or do not follow the selected display."""
    period = 1 / refresh_hz
    median = statistics.median(intervals)
    stable = sum(.65 * period <= dt <= 1.45 * period for dt in intervals) / len(intervals)
    if not .8 * period <= median <= 1.2 * period or stable < .8:
        raise RuntimeError(f'垂直同步节拍检查失败：目标 {refresh_hz:g} Hz，'
                           f'swap 中位间隔 {median*1000:.2f} ms，稳定比例 {stable:.0%}。'
                           '请使用本地显示器、关闭可变刷新率，检查驱动 VSync 设置或改用全屏。')
    return 1 / median


def main():
    parser = argparse.ArgumentParser(description='OpenGL 垂直同步黑白交替；--hz 是颜色切换次数/秒。')
    parser.add_argument('--hz', type=int, choices=(50, 60), default=60)
    parser.add_argument('--fullscreen', action='store_true')
    parser.add_argument('--paused', action='store_true')
    parser.add_argument('--monitor', type=int, default=0, help='显示器序号，默认 0；可用 --list-monitors 查看')
    parser.add_argument('--list-monitors', action='store_true')
    parser.add_argument('--refresh-hz', type=float, help='覆盖节拍校验使用的刷新率，不修改显示模式；默认自动读取')
    args = parser.parse_args()
    if args.refresh_hz is not None and (not math.isfinite(args.refresh_hz) or args.refresh_hz <= 0):
        parser.error('--refresh-hz 必须为正有限数')
    try:
        import glfw
        from OpenGL import GL
    except ImportError as exc:
        parser.exit(1, f'缺少依赖：{exc}\n在运行脚本的电脑安装：python -m pip install glfw PyOpenGL\n')
    if not glfw.init():
        parser.exit(1, 'GLFW 无法初始化，请在支持 OpenGL 的本地图形桌面运行。\n')
    window = None
    try:
        monitors = glfw.get_monitors() or []
        if args.list_monitors:
            for index, monitor in enumerate(monitors):
                mode = glfw.get_video_mode(monitor)
                print(index, glfw.get_monitor_name(monitor), f'{mode.size.width}x{mode.size.height}', f'{mode.refresh_rate} Hz')
            return
        if not 0 <= args.monitor < len(monitors):
            raise ValueError('显示器序号无效，请先执行 --list-monitors')
        monitor = monitors[args.monitor]
        mode = glfw.get_video_mode(monitor)
        refresh = args.refresh_hz or mode.refresh_rate
        if refresh <= 0:
            raise ValueError('无法读取屏幕刷新率，请用 --refresh-hz 指定实际值')
        schedule(refresh, args.hz)
        glfw.window_hint(glfw.DOUBLEBUFFER, glfw.TRUE)
        glfw.window_hint(glfw.CONTEXT_VERSION_MAJOR, 2)
        glfw.window_hint(glfw.CONTEXT_VERSION_MINOR, 1)
        glfw.window_hint(glfw.REFRESH_RATE, mode.refresh_rate)
        width, height = (mode.size.width, mode.size.height) if args.fullscreen else (1000, 720)
        window = glfw.create_window(width, height, 'Black / White VSync', monitor if args.fullscreen else None, None)
        if not window:
            raise RuntimeError('无法创建 OpenGL 双缓冲窗口')
        mx, my = glfw.get_monitor_pos(monitor)
        if not args.fullscreen:
            glfw.set_window_pos(window, mx + 40, my + 40)
        glfw.make_context_current(window)
        glfw.get_error()  # Discard earlier nonfatal initialization diagnostics.
        glfw.swap_interval(1)
        code, description = glfw.get_error()
        if code:
            raise RuntimeError(f'无法启用 swap interval 1：{description}')
        state = dict(hz=args.hz, running=not args.paused, fullscreen=args.fullscreen,
                     frame=0, calibrate=True, focused=True)

        def draw(white):
            value = 1.0 if white else 0.0
            GL.glClearColor(value, value, value, 1.0)
            GL.glClear(GL.GL_COLOR_BUFFER_BIT)
            glfw.swap_buffers(window)

        def key_callback(_window, key, _scan, action, _mods):
            if action != glfw.PRESS:
                return
            if key == glfw.KEY_ESCAPE:
                glfw.set_window_should_close(window, True)
            elif key in (glfw.KEY_5, glfw.KEY_6):
                target = 50 if key == glfw.KEY_5 else 60
                try:
                    schedule(refresh, target)
                except ValueError as exc:
                    print(exc, file=sys.stderr, flush=True)
                    return
                state.update(hz=target, frame=0)
            elif key == glfw.KEY_SPACE:
                state.update(running=not state['running'], frame=0)
            elif key == glfw.KEY_F:
                state['fullscreen'] = not state['fullscreen']
                if state['fullscreen']:
                    glfw.set_window_monitor(window, monitor, 0, 0, mode.size.width, mode.size.height, mode.refresh_rate)
                else:
                    glfw.set_window_monitor(window, None, mx+40, my+40, 1000, 720, 0)
                glfw.swap_interval(1)
                state.update(calibrate=True, frame=0)

        glfw.set_key_callback(window, key_callback)
        print(f'显示器 {args.monitor}：{glfw.get_monitor_name(monitor)}，模式 {refresh:g} Hz。'
              '\n5/6 切换 50/60 次；空格暂停；F 全屏；Esc 退出。'
              '\n启动先显示白色约 1 秒，校验 swap 节拍；失败退出，不回退到软件定时。', flush=True)
        intervals = []
        previous = None
        report_at = time.perf_counter()
        last_description = None
        while not glfw.window_should_close(window):
            glfw.poll_events()
            if glfw.window_should_close(window):
                break
            if glfw.get_window_attrib(window, glfw.ICONIFIED) or not glfw.get_window_attrib(window, glfw.FOCUSED):
                state.update(calibrate=True, frame=0)
                previous = None
                glfw.wait_events_timeout(.1)
                continue
            if state['calibrate']:
                samples = []
                before = time.perf_counter()
                for i in range(72):
                    glfw.poll_events()
                    if glfw.window_should_close(window):
                        return
                    draw(True)
                    now = time.perf_counter()
                    if i >= 12:
                        samples.append(now-before)
                    before = now
                measured = check_pacing(samples, refresh)
                print(f'已请求 VSync=1；swap 节拍检查通过：约 {measured:.2f} 次/秒。'
                      '这是提交节拍校验，不是光学测量。', flush=True)
                state.update(calibrate=False, frame=0)
                intervals.clear()
                previous = None
                report_at = time.perf_counter()
            switch_hz, hold = schedule(refresh, state['hz'])
            description = (state['hz'], state['running'], hold)
            if description != last_description:
                timing = f'每色保持 {hold} 个刷新帧' if hold else '刷新帧数量交替分配，单色持续时间不均匀'
                print(f"目标 {state['hz']} 次切换/秒，调度 {switch_hz:g} 次/秒，"
                      f"{switch_hz/2:g} 个黑白周期/秒；{timing}", flush=True)
                last_description = description
            white = not state['running'] or white_frame(state['frame'], refresh, switch_hz, hold)
            draw(white)
            if state['running']:
                state['frame'] += 1
            now = time.perf_counter()
            if previous is not None:
                intervals.append(now-previous)
            previous = now
            if now-report_at >= 2 and len(intervals) >= 20:
                measured = check_pacing(intervals, refresh)
                long_frames = sum(dt > 1.5/refresh for dt in intervals)
                glfw.set_window_title(window, f"VSync=1 | {state['hz']} switches/s | swap {measured:.1f}/s | late {long_frames} | {'RUN' if state['running'] else 'PAUSED'}")
                if long_frames:
                    print(f'过去一段检测到 {long_frames} 个长帧；该段闪烁间隔不均匀。', flush=True)
                intervals.clear()
                report_at = now
    except (RuntimeError, ValueError) as exc:
        print(f'停止：{exc}', file=sys.stderr)
        raise SystemExit(1)
    finally:
        if window:
            glfw.destroy_window(window)
        glfw.terminate()


if __name__ == '__main__':
    main()
