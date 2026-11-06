# Zephyr native_sim on Android

<p align="center">
  <img src="docs/logo.svg" alt="The Android robot holding the Zephyr kite" width="240">
</p>

Build a Zephyr `native_sim` application for arm64 and run it on an Android phone.
The phone needs no root and no bootloader unlock.

```bash
west build -b native_sim/native/aarch64 -t run <application>
```

| Application | Runs through | Needs on the phone |
| --- | --- | --- |
| Without a screen | adb | USB debugging |
| With a screen, such as LVGL | SSH to Termux | Wi-Fi, Termux, Debian and Termux:X11 |

Tested on a Pixel 3 with Android 12 and Zephyr `v4.5.0-1406-gdecee24b4fb`.

## Setup

You need an arm64 phone, and a Linux machine with a west workspace, Podman and
`adb`. Commands here run from the top of the workspace. From `zephyr/`, leave
out the `zephyr/` in sample paths.

1. Add this project to the workspace, in `zephyr/submanifests/android.yaml`:

   ```yaml
   manifest:
     projects:
       - name: android-native-sim
         url: <url or path of this repository>
         revision: main
         path: android-native-sim
         west-commands: scripts/west-commands.yml
   ```

2. Fetch it and send `west build` through it:

   ```bash
   west update android-native-sim
   west config alias.build android-build
   ```

   Other boards build as before. To undo this: `west config -d alias.build`.

   The alias is set for the whole workspace, but the command comes from the
   submanifest, so `west build` fails with `unknown command "android-build"` on
   a Zephyr branch that doesn't have `submanifests/android.yaml`. Either keep the
   file on the branches you build from, or skip the alias and type
   `west android-build` instead of `west build` for this board.

3. Turn on USB debugging on the phone, connect it, and accept the prompt. It
   should appear in:

   ```bash
   adb devices
   ```

The first build creates the Podman image, which takes several minutes.

## Set the phone's address

Usually you don't need to: with `CONFIG_ANDROID_PHONE_IP` empty, `-t run` uses the
phone adb already sees, and gets its Wi-Fi address from adb when it needs one.

Set it when the phone isn't connected to adb, so `-t run` can reach it over Wi-Fi.
Put it in your `prj.conf`, or give it once on the command line, which the build
directory remembers:

```bash
west build -b native_sim/native/aarch64 -t run <application> -- -DCONFIG_ANDROID_PHONE_IP=\"<phone-ip>\"
```

For adb to connect by address, run this with the phone on USB, once per boot:

```bash
adb tcpip 5555
```

## Applications without a screen

```bash
west build -b native_sim/native/aarch64 -t run zephyr/samples/hello_world
```

The output appears in your terminal, and Ctrl-C stops the application on the
phone.

- After editing the code, `west build -t run` rebuilds and runs.
- To build another application in the same build directory, add `-p always`.
- Leave out `-t run` to only build `build/zephyr/zephyr.exe`.
- With several devices connected, choose one with `ANDROID_SERIAL`.

## Applications with a screen

These draw through SDL, which needs Debian for its libraries and Termux:X11 for
the window. Set up the phone once:

1. Install Termux, and the Termux:X11 APK from the
   [termux-x11 releases](https://github.com/termux/termux-x11/releases). With the
   Google Play build of Termux, use `termux-x11-universal-sharedUid-debug.apk`.

2. In Termux:

   ```bash
   pkg update
   pkg install openssh proot-distro x11-repo
   pkg install termux-x11-nightly
   proot-distro install debian
   proot-distro login debian -- sh -c 'apt-get update && apt-get install -y libsdl2-2.0-0'
   passwd
   sshd
   ```

   Run `sshd` in Termux itself, never from a shell opened with
   `proot-distro login debian`. Termux's programs are on the end of Debian's
   `PATH`, so `sshd` starts there too, but every login then lands inside Debian
   and `-t run` can't open a second Debian.

3. On the Linux machine:

   ```bash
   ssh-copy-id -p 8022 <phone-ip>
   ```

`sshd` has to be started again after the phone reboots. To have Termux start
it, install `termux-services` and run `sv-enable sshd` once.

Keep the phone unlocked, then build and run:

```bash
west build -b native_sim/native/aarch64 -t run zephyr/samples/subsys/display/lvgl
```

This starts the display and opens Termux:X11. The console stays in your terminal,
and Ctrl-C stops the application.

## Sound

Android keeps `/dev/snd` from Debian, so an application that plays through ALSA,
such as one using `CONFIG_I2S_NATIVE_SIM_ALSA`, reaches the speaker through
PulseAudio in Termux. This covers playback only. Set up the phone once, in Termux:

```bash
pkg install pulseaudio
proot-distro login debian -- sh -c 'apt-get update && apt-get install -y --no-install-recommends libasound2-plugins'
proot-distro login debian -- sh -c 'printf "%s\n" "pcm.!default { type pulse server \"127.0.0.1\" }" "ctl.!default { type pulse server \"127.0.0.1\" }" > /etc/asound.conf'
```

Then start PulseAudio, again after each reboot:

```bash
pulseaudio --start --exit-idle-time=-1 --load="module-native-protocol-tcp auth-ip-acl=127.0.0.1 auth-anonymous=1"
```

To check that it runs, use `pactl info`: `pulseaudio --check` can fail over SSH
while the server is up.

## Troubleshooting

| Symptom | Cause | Fix |
| --- | --- | --- |
| `has no libgcc_eh.a, it was built without libc` at configure | The build runs with the host's cross compiler, which can't produce a working `native_sim` executable | `west config alias.build android-build`, or build with `west android-build` |
| `west: unknown command "android-build"` | The checked-out Zephyr branch has no `submanifests/android.yaml`, so the alias points nowhere | Add the submanifest on that branch, or `west config -d alias.build` while working there |
| `proot-distro should not be executed as root user` and `execve("/usr/bin/bash"): No such file or directory` on `-t run` | `sshd` was started inside Debian, so SSH logins land there and the second `proot-distro login` fails | In Termux, `pkill sshd` and then `sshd`, or `sv up sshd` with `termux-services` |
| `no SSH to <ip>: is sshd running in Termux?` | `sshd` isn't running, usually after a reboot | Run `sshd` in Termux |
| `adb cannot reach <ip>:5555` | The phone was rebooted since `adb tcpip 5555` | Connect it by USB and run `adb tcpip 5555` again |

## References

- [Native simulator (native_sim)](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html)
- [Cross-compiling the POSIX architecture](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/cross_compile.html)

## Logo

The Android robot is reproduced or modified from work created and shared by
Google and used according to terms described in the Creative Commons 3.0
Attribution License. The kite is taken from the Zephyr Project logo. Zephyr is a
trademark of the Linux Foundation. This project is not affiliated with or
endorsed by Google or the Linux Foundation.
