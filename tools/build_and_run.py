import argparse
import json
import logging
import shutil
import signal
import subprocess
import sys
import time
from pathlib import Path
from subprocess import CompletedProcess
from typing import Any, Dict, List, Literal, Optional, get_args

import adb
from ansi import Color


class ColorFormatter(logging.Formatter):

    def format(self, record):
        message = super().format(record)
        match record.levelno:
            case logging.ERROR:
                return Color.RED(message)
        return Color.MAGENTA(message)


h = logging.StreamHandler(sys.stdout)
h.setFormatter(ColorFormatter(
    fmt='%(asctime)s [%(name)s] [%(levelname)s] %(message)s',
    datefmt='%m/%d/%Y %H:%M:%S'
))

logging.basicConfig(
    format='%(asctime)s [%(name)s] [%(levelname)s] %(message)s',
    level=logging.INFO,
    datefmt='%m/%d/%Y %H:%M:%S',
    handlers=[
        h
    ]
)


def get_logger(name: str):
    return logging.getLogger(name)


logger = get_logger('Build Script')

PACKAGE_NAME_BEAT_SABER = 'com.beatgames.beatsaber'

is_app_running = False

# Capture the original signal handler
original_on_sig_int = signal.getsignal(signal.SIGINT)


def on_sig_int(sig: int, frame):
    if is_app_running:
        stop_app(PACKAGE_NAME_BEAT_SABER)
    else:
        logger.error('User exited with CTRL-C')
        exit(1)
        if callable(original_on_sig_int):
            original_on_sig_int(sig, frame)


signal.signal(signal.SIGINT, on_sig_int)


def adb_shell(command: List[str]) -> CompletedProcess:
    process = subprocess.run([
        'adb',
        'shell',
        *command
    ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return process


def stop_app(app_id: str):
    process = subprocess.run([
        'adb',
        'shell',
        'am',
        'force-stop',
        app_id
    ], stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if process.returncode != 0:
        logger.error(f'Failed to stop app: {app_id}')
        logger.error(process.stdout.decode('utf-8'))


def start_app(app_id: str):
    process = adb_shell([
        'am',
        'start',
        'com.beatgames.beatsaber/com.unity3d.player.UnityPlayerActivity'
    ])
    if process.returncode != 0:
        logger.error(f'Failed to start app: {app_id}')
        logger.error(process.stdout.decode('utf-8'))


BuildType = Literal['debug', 'release']


class Context:

    def __init__(
            self,
            *,
            project_root_dir: Path,
            project_output_dir: Path,
            build_type: BuildType
    ):
        self._project_root_dir = project_root_dir
        self._project_output_dir = project_output_dir
        self._build_type = build_type

    @property
    def project_root_dir(self) -> Path:
        return self._project_root_dir

    @property
    def project_output_dir(self) -> Path:
        return self._project_output_dir

    @property
    def build_type(self) -> BuildType:
        return self._build_type


def build(context: Context):
    project_dir = context.project_root_dir.resolve().absolute()
    output_dir = context.project_output_dir.resolve().absolute()

    # Determine the CMake build type
    cmake_build_type = None
    match context.build_type:
        case 'debug':
            cmake_build_type = 'Debug'
        case 'release':
            cmake_build_type = 'RelWithDebInfo'
        case _:
            raise ValueError(f'Unknown build type: {context.build_type}')

    # Configure CMake
    logger.info(f'Configuring CMake project...')
    process = subprocess.run([
        'cmake',
        '-G',
        'Ninja',
        f'-DCMAKE_BUILD_TYPE={cmake_build_type}',
        '-B',
        str(output_dir)
    ], cwd=project_dir, stderr=subprocess.STDOUT)
    if process.returncode != 0:
        raise RuntimeError(f'Command Failed! Exit Code = {process.returncode}')

    # Build project
    logger.info(f'Building CMake project...')
    process = subprocess.run([
        'cmake',
        '--build',
        str(output_dir)
    ], cwd=project_dir, stderr=subprocess.STDOUT)
    if process.returncode != 0:
        raise RuntimeError(f'Command Failed! Exit Code = {process.returncode}')


def create_qmod(
        *,
        mod_json_path: str | Path,
        project_dir: str | Path,
        build_output_dir: str | Path
):
    mod_json_path = Path(mod_json_path).absolute()
    project_dir = Path(project_dir).absolute()
    build_output_dir = Path(build_output_dir).absolute()

    # Load mod.json
    mod_json_content: Optional[Dict[str, Any]] = None
    with open(mod_json_path) as file:
        mod_json_content = json.load(file)
    if not mod_json_content:
        raise RuntimeError('Bad JSON!')

    mod_info_json = mod_json_content['info']
    mod_id = mod_info_json['id']
    mod_version = mod_info_json['version']
    qmod_path = build_output_dir / f'{mod_id}-v{mod_version}.qmod'

    process = subprocess.run([
        'qpm',
        'qmod',
        'zip',
        str(qmod_path)
    ], cwd=project_dir, stderr=subprocess.STDOUT)
    if process.returncode != 0:
        raise RuntimeError(f'Command Failed! Exit Code = {process.returncode}')


def deploy(context: Context):
    mod_json = None
    with open('../mod.template.json', 'r') as file:
        mod_json = json.load(file)
    mod_files = mod_json['modFiles']
    late_mod_files = mod_json['lateModFiles']

    lib_src_dir = context.project_output_dir

    for mod_file in mod_files:
        src_path = lib_src_dir / mod_file
        dst_path = '/sdcard/ModData/com.beatgames.beatsaber/Modloader/early_mods/'
        logger.info(f'Deploying "{src_path}" -> "{dst_path}"')
        result = adb.push(src_path, dst_path)
        if result.returncode != 0:
            raise RuntimeError(f'Command Failed! Exit Code = {result.returncode}')

    for mod_file in late_mod_files:
        src_path = lib_src_dir / mod_file
        dst_path = '/sdcard/ModData/com.beatgames.beatsaber/Modloader/mods/'
        logger.info(f'Deploying "{src_path}" -> "{dst_path}"')
        result = adb.push(src_path, dst_path)
        if result.returncode != 0:
            raise RuntimeError(f'Command Failed! Exit Code = {result.returncode}')


def main():
    # Parse arguments
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--clean',
        action='store_true',
        help='Do a clean build'
    )
    parser.add_argument(
        '--build-type',
        '-t',
        choices=get_args(BuildType),
        default='debug'
    )
    parser.add_argument(
        '--build-only',
        action='store_true'
    )
    args = parser.parse_args()

    # Assign arguments
    clean: bool = args.clean
    build_type: BuildType = args.build_type
    is_build_only: bool = args.build_only

    project_root_dir = (Path(__file__) / '..' / '..').resolve()
    build_output_dir = project_root_dir / 'build'

    # Create context
    context = Context(
        project_root_dir=project_root_dir,
        project_output_dir=build_output_dir,
        build_type=build_type
    )

    # Clean build output
    if clean:
        logger.info(f'Cleaning build output at "{build_output_dir.absolute()}"')
        if build_output_dir.exists():
            shutil.rmtree(build_output_dir)

    # Build
    start_time = time.time()
    build(context)
    logger.info(f'Build finished in {int(time.time() - start_time)} seconds.')

    # Create .qmod file
    create_qmod(
        mod_json_path=project_root_dir / 'qpm.json',
        project_dir=project_root_dir,
        build_output_dir=build_output_dir
    )

    if is_build_only:
        return

    # Push mod files
    deploy(context)

    # Launch game
    stop_app(PACKAGE_NAME_BEAT_SABER)
    start_app(PACKAGE_NAME_BEAT_SABER)
    global is_app_running
    is_app_running = True

    # Start logging
    process = subprocess.run([
        'python',
        str(Path('tools/log.py')),
        '--exit-on-disconnect',
        '--pid',
        'com.beatgames.beatsaber',
        '--tag',
        'spotify-search'
    ], cwd=project_root_dir)
    if process.returncode != 0:
        logger.error(f'Logging Failed! Exit Code = {process.returncode}')
        return


if __name__ == '__main__':
    try:
        main()
    except Exception as e:
        logger.error(str(e))
        exit(1)
