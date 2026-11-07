---
title: Troubleshooting
nav_order: 7
---

# Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| `has no libgcc_eh.a, it was built without libc` at configure | The build runs with the host's cross compiler, which can't produce a working `native_sim` executable | `west config alias.build android-build`, or build with `west android-build` |
| `west: unknown command "android-build"` | The checked-out Zephyr branch has no `submanifests/android.yaml`, so the alias points nowhere | Add the submanifest on that branch, or `west config -d alias.build` while working there |
| `proot-distro should not be executed as root user` and `execve("/usr/bin/bash"): No such file or directory` on `-t run` | `sshd` was started inside Debian, so SSH logins land there and the second `proot-distro login` fails | In Termux, `pkill sshd` and then `sshd`, or `sv up sshd` with `termux-services` |
| `no SSH to <ip>: is sshd running in Termux?` | `sshd` isn't running, usually after a reboot | Run `sshd` in Termux |
| `adb cannot reach <ip>:5555` | The phone was rebooted since `adb tcpip 5555` | Connect it by USB and run `adb tcpip 5555` again |
