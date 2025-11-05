import subprocess
from pathlib import Path, PosixPath
from subprocess import CompletedProcess


def push(src_path: str | Path, dst_path: str | PosixPath) -> CompletedProcess:
    src_path = Path(src_path).expanduser().resolve()
    dst_path = Path(dst_path)
    return subprocess.run(
        ['adb', 'push', str(src_path), dst_path.as_posix()]
    )
