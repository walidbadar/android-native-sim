#!/usr/bin/env python3
# Copyright (c) 2026 Muhammad Waleed Badar
# SPDX-License-Identifier: GPL-3.0-only
"""Run a native_sim binary on an Android phone.

An application with a screen runs in Debian in Termux, over SSH, and draws in
Termux:X11. Any other application runs from the adb shell.
"""

import re
import subprocess
import sys
from pathlib import Path

ADB_PORT = 5555
ADB_DIR = '/data/local/tmp/zephyr'
ADB_STOP = 'pkill -x zephyr.exe; true'
# Termux accepts any user name, so the address is all SSH needs
SSH_PORT = '8022'
TERMUX_STOP = ('for p in $(ps -A -o pid=,args= | awk \'$2 ~ /zephyr\\.exe$/ {print $1}\'); '
               'do kill -9 "$p"; done')
TERMUX_DISPLAY = ('ps -A -o args= | grep -q "^termux-x11 com.termux.x11" || '
                  '(nohup setsid termux-x11 :0 >/dev/null 2>&1 </dev/null &); '
                  'am start -n com.termux.x11/.MainActivity >/dev/null 2>&1; true')
TERMUX_RUN = ('proot-distro login debian --shared-tmp '
              '--bind $HOME/zephyr.exe:/root/zephyr.exe -- '
              'env DISPLAY=:0 LIBGL_ALWAYS_SOFTWARE=1 /root/zephyr.exe')
SIGINT_EXIT = 130


def read_config(exe):
    text = (exe.parent / '.config').read_text()
    ip = re.search(r'^CONFIG_ANDROID_PHONE_IP="(.*)"$', text, re.M)
    return ip.group(1) if ip else '', 'CONFIG_SDL_DISPLAY=y' in text


def terminal():
    # With a terminal, Ctrl-C reaches the application through adb or ssh
    return '-t' if sys.stdin.isatty() else '-T'


def run(command, stop):
    try:
        code = subprocess.run(command).returncode
    except KeyboardInterrupt:
        code = SIGINT_EXIT
    finally:
        subprocess.run(stop, capture_output=True)
    return 0 if code == SIGINT_EXIT else code


def adb(serial, *command):
    full = ['adb', *(['-s', serial] if serial else []), *command]
    result = subprocess.run(full, capture_output=True, text=True)
    if result.returncode:
        sys.exit(f'adb {command[0]} failed: {(result.stderr or result.stdout).strip()}\n'
                 'Is USB debugging on? With several devices, set ANDROID_SERIAL.')
    return result.stdout.strip()


def run_adb(exe, ip):
    serial = None
    if ip:
        serial = f'{ip}:{ADB_PORT}'
        reply = subprocess.run(['adb', 'connect', serial], capture_output=True, text=True)
        if 'connected to' not in reply.stdout:
            sys.exit(f'adb cannot reach {serial}: {(reply.stdout + reply.stderr).strip()}\n'
                     f'With the phone on USB, run: adb tcpip {ADB_PORT}')

    abi = adb(serial, 'shell', 'getprop', 'ro.product.cpu.abi')
    if abi != 'arm64-v8a':
        sys.exit(f'the device is {abi}, only arm64-v8a is supported')

    # A running binary cannot be overwritten
    adb(serial, 'shell', f'{ADB_STOP}; mkdir -p {ADB_DIR}')
    adb(serial, 'push', str(exe), f'{ADB_DIR}/zephyr.exe')
    prefix = ['adb', *(['-s', serial] if serial else []), 'shell']
    return run([*prefix, terminal(),
                f'cd {ADB_DIR} && chmod 755 zephyr.exe && exec ./zephyr.exe'],
               [*prefix, ADB_STOP])


def phone_ip():
    """The phone's Wi-Fi address, if adb can reach it."""
    result = subprocess.run(['adb', 'shell', 'ip', '-4', '-o', 'addr', 'show', 'wlan0'],
                            capture_output=True, text=True)
    found = re.search(r'inet ([0-9.]+)', result.stdout)
    return found.group(1) if found else None


def run_termux(exe, ip):
    ip = ip or phone_ip()
    if not ip:
        sys.exit('Cannot find the phone\'s address through adb: connect it, '
                 'or set CONFIG_ANDROID_PHONE_IP')

    ssh = ['ssh', '-p', SSH_PORT, ip]
    if subprocess.run([*ssh, f'{TERMUX_STOP}; {TERMUX_DISPLAY}']).returncode:
        sys.exit(f'no SSH to {ip}: is sshd running in Termux?')
    subprocess.run(['scp', '-q', '-P', SSH_PORT, str(exe), f'{ip}:zephyr.exe'], check=True)
    return run([ssh[0], terminal(), *ssh[1:], TERMUX_RUN], [*ssh, TERMUX_STOP])


def main():
    exe = Path(sys.argv[1])
    ip, screen = read_config(exe)
    return run_termux(exe, ip) if screen else run_adb(exe, ip)


if __name__ == '__main__':
    sys.exit(main())
