---
title: Without a display
nav_order: 3
---

# Applications without a display

```bash
west build -b native_sim/native/aarch64 -t run zephyr/samples/hello_world
```

The output appears in your terminal, and Ctrl-C stops the application on the
phone.

- After editing the code, `west build -t run` rebuilds and runs.
- To build another application in the same build directory, add `-p always`.
- Leave out `-t run` to only build `build/zephyr/zephyr.exe`.
- With several devices connected, choose one with `ANDROID_SERIAL`.
