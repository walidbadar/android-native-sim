# Zephyr native_sim on Android

Build a Zephyr `native_sim` application for arm64 on a Linux machine and run it
on an Android phone, using one board and one command. The phone needs no root and
no bootloader unlock.

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

3. On the Linux machine:

   ```bash
   ssh-copy-id -p 8022 <phone-ip>
   ```

`sshd` has to be started again after the phone reboots.

Keep the phone unlocked, then build and run:

```bash
west build -b native_sim/native/aarch64 -t run zephyr/samples/subsys/display/lvgl
```

This starts the display and opens Termux:X11. The console stays in your terminal,
and Ctrl-C stops the application.

## Troubleshooting

| Problem | Fix |
| --- | --- |
| `unknown command "android-build"` | `zephyr/submanifests/android.yaml` is missing, as `git clean` removes it. Add it again, or run `west config -d alias.build` to build other boards |
| `gcc has no libgcc_eh.a` | `west build` isn't using the cross image. Run `west config alias.build android-build` |
| `Cannot find the phone's address` | Connect the phone to adb and Wi-Fi, or set `CONFIG_ANDROID_PHONE_IP` |
| `adb cannot reach` | Run `adb tcpip 5555` with the phone on USB |
| `no SSH to` | Start `sshd` in Termux |
| `no devices/emulators found` | Check USB debugging and `adb devices` |
| Black screen | Unlock the phone and open Termux:X11 |

## References

- [Native simulator (native_sim)](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html)
- [Cross-compiling the POSIX architecture](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/cross_compile.html)
