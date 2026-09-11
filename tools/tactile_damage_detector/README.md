# Tactile damage detector

输入一张触觉基线图和一张待检测图，使用 UGripper `solid-core-tone-lut-v2` 单帧算法输出损坏结论与最终实心核心 mask。

## 环境

- Linux
- Python 3.9+
- NumPy、Pillow：`python3 -m pip install -r requirements.txt`

本目录包含完整算法，不读取或调用仓库中的其他脚本。启动器默认使用 `/usr/bin/python3`，可通过 `TACTILE_DAMAGE_PYTHON=/path/to/python` 指定其他解释器；启动时会移除交叉编译环境遗留的 `LD_LIBRARY_PATH`，避免误加载目标板动态库。

## 使用

```bash
./detect \
  --baseline baseline.png \
  --image current.png \
  --output-dir result
```

普通相机原图会按照运行时口径裁掉左侧约 `18%` 光源区域，再缩放为 `160x120` 灰度图。输入已经是裁剪后的 `160x120` 图时，增加 `--preprocessed`。

输出目录固定包含：

- `result.json`：`damaged` 结论、算法版本、输入路径、检测耗时、实心核心像素数及诊断指标。
- `mask.png`：最终参与损坏判定的二值实心核心 mask。
- `comparison.png`：基线、输入图和结果图；损坏 mask 为红色，基线不兼容为黄色。

进程返回码：`0` 表示未损坏，`2` 表示检测为损坏，`1` 表示输入或运行错误。批处理调用时不要把返回码 `2` 当成工具执行失败。

算法默认过滤无法形成至少 `8` 个 `3x3` 实心核心像素的细线、曲线和贴边轮廓。此工具只给出单张图片结论；设备黄灯的连续 episode 策略属于运行时决策层。

明显全局明暗变化时，算法会尝试九分位单调亮度 LUT；仅当平均亮度偏移至少 `8`、残差 P99 不超过 `8.5` 且较线性校正下降至少 `40%`、结构相关性同时提高时才采用，否则自动回退原线性校正。

若全局相关性低于 `0.90`，算法进一步比较 `10x16` 个局部纹理块：几乎没有匹配块时输出 `damage_reason=baseline_mismatch`、`baseline_refresh_required=true`，不作为损坏；仍保留匹配区域时输出 `large_structural_damage`，防止大面积局部损坏被当成换盖板。仅凭两张图无法绝对区分“整面完全损坏”和“整张盖板更换”，因此工具只提示更新基线，不会自动覆盖输入基线。

## RK3588 性能参考

2026-09-11 在 `192.168.2.240`（aarch64）实测：与设备运行时相同的 C++ 核心算法，每类预热后循环 500 次，普通帧、亮度 LUT、真实损坏和换盖板门禁的平均耗时分别为 `3.296 ms`、`8.184 ms`、`4.587 ms`、`8.825 ms`，最重路径 P95 为 `8.957 ms`。

本目录的独立 Python 工具会额外执行进程启动、PNG 解码和 `result.json`、`mask.png`、`comparison.png` 落盘；四类输入各独立运行 10 次，端到端平均耗时为 `282.797–306.113 ms`。设备在线黄灯检测使用 C++ 核心耗时，不包含独立工具的启动和制图开销。
