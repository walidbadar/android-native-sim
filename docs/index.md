---
title: Home
nav_order: 1
permalink: /
---

# Zephyr native_sim on Android

<p align="center">
  <img src="{{ '/logo.svg' | relative_url }}" alt="The Android robot holding the Zephyr kite" width="240">
</p>

Build a Zephyr `native_sim` application for arm64 and run it on an Android phone.
The phone needs no root and no bootloader unlock.

```bash
west build -b native_sim/native/aarch64 -t run <application>
```

| Application | Runs through | Needs on the phone |
| --- | --- | --- |
| [Without a display](headless.md) | adb | USB debugging |
| [With a display](display.md), such as LVGL | SSH to Termux | Wi-Fi, Termux, Debian and Termux:X11 |

Applications can also play [sound](sound.md).

Tested on a Pixel 3 with Android 12 and Zephyr `v4.5.0-1406-gdecee24b4fb`.

Start with [Setup](setup.md).

## References

- [Native simulator (native_sim)](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/index.html)
- [Cross-compiling the POSIX architecture](https://docs.zephyrproject.org/latest/boards/native/native_sim/doc/cross_compile.html)
