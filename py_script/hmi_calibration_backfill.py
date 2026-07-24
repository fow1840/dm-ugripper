#!/usr/bin/env python3
from __future__ import annotations

import argparse


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="已停用：相机/IMU 标定不再写入 HMI payload。"
    )
    parser.add_argument(
        "--calib-root",
        default=None,
        help="已弃用，保留用于兼容旧命令",
    )
    parser.add_argument(
        "--ports",
        nargs="*",
        default=[],
        help="已弃用，保留用于兼容旧命令",
    )
    parser.add_argument(
        "--require-sn-prefix",
        default="",
        help="已弃用，保留用于兼容旧命令",
    )
    return parser.parse_args()


def main() -> int:
    parse_args()
    print(
        "该工具已停用：相机/IMU 标定保存在相机模组，HMI payload 仅用于与 SN 绑定的力传感器标定。"
    )
    print(
        "请使用 /opt/ugripper/bin/GripperHmiTool/gripper_hmi_dashboard.py "
        "的两点标定流程写入 HMI 力标定。"
    )
    return 2


if __name__ == "__main__":
    raise SystemExit(main())
