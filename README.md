# Zephyr native_sim on Android

<p align="center">
  <img src="docs/logo.svg" alt="The Android robot holding the Zephyr kite" width="240">
</p>

Build a Zephyr `native_sim` application for arm64 and run it on an Android phone.
The phone needs no root and no bootloader unlock.

```bash
west build -b native_sim/native/aarch64 -t run zephyr/samples/hello_world
```

**Documentation: <https://walidbadar.github.io/android-native-sim/>**

- [Setup](https://walidbadar.github.io/android-native-sim/setup.html)
- [Applications without a screen](https://walidbadar.github.io/android-native-sim/headless.html)
- [Applications with a screen](https://walidbadar.github.io/android-native-sim/display.html)
- [Sound](https://walidbadar.github.io/android-native-sim/sound.html)
- [Location](https://walidbadar.github.io/android-native-sim/location.html)
- [Troubleshooting](https://walidbadar.github.io/android-native-sim/troubleshooting.html)

## Logo

The Android robot is reproduced or modified from work created and shared by
Google and used according to terms described in the Creative Commons 3.0
Attribution License. The kite is taken from the Zephyr Project logo. Zephyr is a
trademark of the Linux Foundation. This project is not affiliated with or
endorsed by Google or the Linux Foundation.
