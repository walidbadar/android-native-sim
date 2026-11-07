---
title: Setup
nav_order: 2
---

# Setup

You need an arm64 phone, and a Linux machine with a west workspace, Podman and
`adb`. Commands here run from the top of the workspace. From `zephyr/`, leave
out the `zephyr/` in sample paths.

1. Add this project to the workspace, in `zephyr/submanifests/android.yaml`:

   ```yaml
   manifest:
     projects:
       - name: android-native-sim
         url: https://github.com/walidbadar/android-native-sim
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
