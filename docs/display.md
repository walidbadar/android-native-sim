---
title: With a display
nav_order: 4
---

# Applications with a display

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

   {: .warning }
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
