---
title: Location
nav_order: 6
---

# Location

The `zephyr,native-sim-gnss` driver in this module gives a `native_sim`
application the phone's location through the Zephyr GNSS API. It reads
`dumpsys location`, which the adb shell user may call, so no app is needed on
the phone. Add the node to the application's `boards/native_sim_native_64.overlay`:

```dts
gnss: gnss {
        compatible = "zephyr,native-sim-gnss";
        status = "okay";
};
```

```bash
west build -b native_sim/native/aarch64 -t run android-native-sim/samples/gnss
```

The same build runs on the laptop for `native_sim/native/64`, reading the phone
through adb over USB. With `-gnss_position=<lat>,<lon>[,<alt>]` it reports a
fixed position instead and needs no phone.

See the [GNSS sample](https://github.com/walidbadar/android-native-sim/blob/main/samples/gnss/README.md)
for running it in Termux without the laptop, and for what the fixes contain.
