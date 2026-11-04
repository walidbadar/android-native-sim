# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only
"""west build for native_sim/native/aarch64: build in the Debian image.

"-t run" then runs the result on the phone. Other boards behave exactly as with
the original command. Zephyr's "build" command cannot be replaced by an extension,
so this one is reached through the alias "west config alias.build android-build".
"""

import configparser
import subprocess
import sys
import tempfile
from pathlib import Path

from west import log
from west.util import west_topdir

sys.path.insert(0, str(Path(west_topdir()) / 'zephyr/scripts/west_commands'))
from build import Build  # noqa: E402

IMAGE = 'zephyr-android-cross'
BOARD = 'native_sim/native/aarch64'
PROJECT = Path(__file__).resolve().parent.parent
ANDROID_RUN = 'boards/native/native_sim_aarch64/android_run.py'
BUILD_DIR = 'build'


def inner_args():
    """The user's "west build" arguments without "-t run", which is done out here."""
    argv = sys.argv[1:]
    start = next(i for i, arg in enumerate(argv) if arg in ('build', 'android-build'))
    args = iter(argv[start + 1:])
    kept = []
    for arg in args:
        if arg == '--':
            kept += [arg, *args]
        elif arg in ('-t', '--target'):
            target = next(args, '')
            if target != 'run':
                kept += [arg, target]
        elif arg not in ('-trun', '--target=run'):
            kept.append(arg)
    return kept


class AndroidBuild(Build):
    def __init__(self):
        super().__init__()
        self.name = 'android-build'

    def do_run(self, args, remainder):
        if (args.board or self.cached(args, 'CACHED_BOARD')) != BOARD:
            super().do_run(args, remainder)
            return

        build = Path(args.build_dir or BUILD_DIR).resolve()
        self.cross_build(args, remainder, build)
        if args.target == 'run':
            exe = build / 'zephyr/zephyr.exe'
            sys.exit(subprocess.run([sys.executable, PROJECT / ANDROID_RUN, exe]).returncode)

    def cached(self, args, name):
        """A value from an existing build, or None."""
        cache = Path(args.build_dir or BUILD_DIR) / 'CMakeCache.txt'
        if cache.exists():
            for line in cache.read_text().splitlines():
                if line.startswith(f'{name}:'):
                    return line.split('=', 1)[1]
        return None

    def mounts(self, source):
        top = Path(west_topdir()).resolve()
        paths = {top, Path.cwd()}
        # Projects can be symlinks out of the workspace
        paths |= {Path(p.abspath).resolve() for p in self.manifest.projects if p.abspath}
        if source:
            # The whole repository: applications use modules next to them
            git = subprocess.run(['git', '-C', source, 'rev-parse', '--show-toplevel'],
                                 capture_output=True, text=True)
            paths.add(Path(git.stdout.strip() if git.returncode == 0 else source).resolve())
        return sorted(str(p) for p in paths if p.exists())

    def cross_build(self, args, remainder, build):
        top = Path(west_topdir()).resolve()
        if subprocess.run(['podman', 'image', 'exists', IMAGE]).returncode:
            log.banner(f'building the {IMAGE} image, this happens once')
            subprocess.run(['podman', 'build', '-q', '-t', IMAGE, '-f',
                            str(PROJECT / 'docker/Containerfile'),
                            str(top / 'zephyr/scripts')], check=True)

        build.mkdir(parents=True, exist_ok=True)
        west = ['west', 'build']
        if not args.build_dir:
            west += ['-d', BUILD_DIR]
        west += inner_args()

        source = remainder[0] if remainder and remainder[0] != '--' else args.source_dir
        source = source or self.cached(args, 'CMAKE_HOME_DIRECTORY')
        # Sources are read-only and keep their real paths in the container
        volumes = [a for m in self.mounts(source) for a in ('-v', f'{m}:{m}:ro')]
        volumes += ['-v', f'{build}:{build}']

        # build.cmake-args describes the host, and the alias would bring the
        # container back here
        config = configparser.ConfigParser(interpolation=None)
        config.read(top / '.west/config')
        if config.has_section('build'):
            config.remove_option('build', 'cmake-args')
        config.remove_section('alias')
        with tempfile.NamedTemporaryFile('w') as west_config:
            config.write(west_config)
            west_config.flush()
            result = subprocess.run(
                ['podman', 'run', '--rm', '--security-opt', 'label=disable',
                 *(['-it'] if sys.stdin.isatty() else []), *volumes,
                 '-v', f'{west_config.name}:/west-config:ro',
                 '-e', 'WEST_CONFIG_LOCAL=/west-config',
                 '-e', f'ZEPHYR_BASE={top}/zephyr',
                 '-e', 'GIT_CONFIG_COUNT=1', '-e', 'GIT_CONFIG_KEY_0=safe.directory',
                 '-e', 'GIT_CONFIG_VALUE_0=*',
                 '-w', str(Path.cwd()), IMAGE, *west])
        if result.returncode:
            self.die('build failed')
