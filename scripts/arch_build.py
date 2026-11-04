# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only
"""west build --arch: cross-compile native_sim for another architecture.

Zephyr's "build" command cannot be replaced by an extension, so this one is
reached through the alias "west config alias.build arch-build". Without --arch
it behaves exactly like the original.
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

IMAGE = 'zephyr-pixel-cross'
BOARD = 'native_sim/native/64'
PROJECT = Path(__file__).resolve().parent.parent
# Target architectures with a toolchain in the image
TOOLCHAINS = {'aarch64': '/usr/bin/aarch64-linux-gnu-'}


def inner_args():
    """The user's "west build" arguments without --arch."""
    argv = sys.argv[1:]
    start = next(i for i, arg in enumerate(argv) if arg in ('build', 'arch-build'))
    args = iter(argv[start + 1:])
    kept = []
    for arg in args:
        if arg == '--':
            kept += [arg, *args]
        elif arg == '--arch':
            next(args, '')
        elif not arg.startswith('--arch='):
            kept.append(arg)
    return kept


class ArchBuild(Build):
    def __init__(self):
        super().__init__()
        self.name = 'arch-build'

    def do_add_parser(self, parser_adder):
        parser = super().do_add_parser(parser_adder)
        parser.add_argument('--arch', choices=sorted(TOOLCHAINS),
                            help='cross-compile native_sim for this architecture '
                                 'instead of the host one. The binary is dynamically '
                                 'linked against Debian 13 glibc. Build directory '
                                 'defaults to build-<arch>')
        return parser

    def do_run(self, args, remainder):
        if not args.arch:
            super().do_run(args, remainder)
            return

        if args.board and args.board != BOARD:
            self.die(f'--arch needs -b {BOARD}, not {args.board}')
        if args.target == 'run':
            self.die('-t run cannot start a binary for another architecture; '
                     'copy zephyr/zephyr.exe to the target')
        self.cross_build(args, remainder)

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

    def cross_build(self, args, remainder):
        top = Path(west_topdir()).resolve()
        if subprocess.run(['podman', 'image', 'exists', IMAGE]).returncode:
            log.banner(f'building the {IMAGE} image, this happens once')
            subprocess.run(['podman', 'build', '-q', '-t', IMAGE, '-f',
                            str(PROJECT / 'docker/Containerfile'),
                            str(top / 'zephyr/scripts')], check=True)

        default_dir = f'build-{args.arch}'
        build = Path(args.build_dir or default_dir).resolve()
        build.mkdir(parents=True, exist_ok=True)

        user_args = inner_args()
        west = ['west', 'build']
        if not args.build_dir:
            west += ['-d', default_dir]
        if not args.board:
            west += ['-b', BOARD]
        west += user_args
        if '--' not in user_args:
            west.append('--')
        west += [f'-DNATIVE_TARGET_HOST={args.arch}',
                 '-DZEPHYR_TOOLCHAIN_VARIANT=cross-compile',
                 f'-DCROSS_COMPILE={TOOLCHAINS[args.arch]}']

        source = remainder[0] if remainder and remainder[0] != '--' else args.source_dir
        # Sources are read-only and keep their real paths in the container
        volumes = [a for m in self.mounts(source) for a in ('-v', f'{m}:{m}:ro')]
        volumes += ['-v', f'{build}:{build}']

        # build.cmake-args describes the host, and would override the defines above
        config = configparser.ConfigParser(interpolation=None)
        config.read(top / '.west/config')
        if config.has_section('build'):
            config.remove_option('build', 'cmake-args')
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
