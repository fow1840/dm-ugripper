#!/usr/bin/env python3
"""CLI for the same capture workflow used by the Web buttons."""
import argparse
import json
from pathlib import Path
import sys
import time
from camera_snapshot import capture
from common import operation_lock

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description='按采集参数抓图；双目复用预热帧')
    parser.add_argument('output', nargs='?', default='/tmp/getimg_' + time.strftime('%Y%m%d_%H%M%S'))
    parser.add_argument('--ego', action='store_true', help='仅 Ego 短录 3 秒，截帧成功后删除远端测试数据')
    args = parser.parse_args()
    try:
        with operation_lock():
            result = capture('ego' if args.ego else 'local', Path(args.output))
        print(json.dumps(result, ensure_ascii=False, indent=2))
        sys.exit(0 if result['status'] == 'ok' else 1)
    except Exception as exc:
        print(str(exc), file=sys.stderr)
        sys.exit(1)
