"""Host smoke test: absent encoders must fail closed and finalize both MCAPs."""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    if any(Path(path).exists() for path in ("/dev/left_encoder", "/dev/right_encoder")):
        print("SKIP: hardware encoder nodes exist on this host")
        return 77
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix="encoder-startup-") as directory:
        read_fd, write_fd = os.pipe()
        try:
            result = subprocess.run(
                [binary, directory, "--ready-fd", str(write_fd)],
                cwd=directory, pass_fds=(write_fd,), capture_output=True,
                text=True, timeout=10,
            )
        finally:
            os.close(write_fd)
        try:
            assert os.read(read_fd, 1) == b"", "failed startup incorrectly signaled readiness"
        finally:
            os.close(read_fd)
        assert result.returncode == 32, (result.returncode, result.stdout, result.stderr)
        assert "error_code=UGR-30-02" in result.stdout + result.stderr
        for side in ("left", "right"):
            content = (Path(directory) / f"sensor_{side}.mcap").read_bytes()
            assert content.startswith(b"\x89MCAP0\r\n")
            assert content.endswith(b"\x89MCAP0\r\n"), f"{side} MCAP was not finalized"
    print("Absent encoders: readiness rejected, exit=32, both MCAPs finalized")
    return 0


if __name__ == "__main__":
    sys.exit(main())
