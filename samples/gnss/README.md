# GNSS fixes from an Android phone

Prints each fix from the `zephyr,native-sim-gnss` driver. `src/main.c` and
`prj.conf` are portable: any board with a `gnss` alias on a GNSS device builds
them. `boards/native_sim_native_64.overlay` adds the native_sim receiver, and
this module applies it to `native_sim/native/aarch64` too.

## The driver

- `drivers/gnss/gnss_native_sim.c`, the Zephyr side, publishes a `gnss_data`
  once per fix interval (1 s by default). It implements `gnss_start`,
  `gnss_stop`, `gnss_set_fix_rate` and `gnss_get_fix_rate`, and starts
  streaming at boot, like the NMEA receivers. One instance is supported, since
  there is one host location.
- `drivers/gnss/gnss_native_sim_bottom.c`, the host side, polls the phone once
  a second on a host thread, so a slow `dumpsys` or adb call never stalls the
  simulation.
- `tests/drivers/gnss/native_sim` tests it with a fixed position.

The location comes from `dumpsys location`, run directly when the executable
is on the phone as the adb shell user, and through adb otherwise. With several
devices on adb, choose one with `ANDROID_SERIAL`. `-gnss_position=<lat>,<lon>[,<alt>]`
(degrees, metres above sea level) reports a fixed position instead.

## On the laptop

```bash
west build -b native_sim/native/64 android-native-sim/samples/gnss
build/zephyr/zephyr.exe
build/zephyr/zephyr.exe -gnss_position=52.370216,4.895168,12
```

## On the phone

`west build -b native_sim/native/aarch64 -t run android-native-sim/samples/gnss`
starts it through `adb shell`, as the shell user, which may call `dumpsys`
directly. The laptop stays in the loop.

To run it without the laptop, copy `build/zephyr/zephyr.exe` to the phone and
start it in Termux's Debian. Termux cannot execute a non-PIE static binary
itself, so go through proot with the home directory bound in:

```bash
proot-distro login debian --bind $HOME:/mnt/home -- bash -lc 'cd /mnt/home && ./zephyr.exe'
```

Inside Termux the binary runs as the app user, which may not call `dumpsys`, so
it goes through the phone's own adb client (`pkg install android-tools`)
connected to the phone's Wireless debugging port. Pair once with the dialog
open (Developer options, Wireless debugging, "Pair device with pairing code"):

```bash
adb pair <ip>:<pairing port> <code>
adb connect <ip>:<port shown on the Wireless debugging page>
```

The connect port changes after a reboot or when Wireless debugging is toggled,
so repeat the `adb connect` then. Pairing persists. The pairing dialog closes
when Settings leaves the screen, so run `adb pair` from an SSH session or a
split-screen Termux while it is showing.

## What the fixes contain

The driver prefers the `gps` provider (a GNSS fix) and falls back to `fused` or
`network` (an estimated fix, no satellites). While neither dumpsys nor adb gives
a location it publishes "no fix" and warns once.

`dumpsys location` reports the last fix each provider delivered, which only
updates while some app on the phone is requesting location. Open Maps or any
navigation app on the phone to get a live GPS track; with nothing requesting
location the position stays at the last known one. Android does not expose the
satellite count or HDOP in that dump, so a GNSS fix reports 8 satellites and the
HDOP is the horizontal accuracy divided by 5 m (at least 0.5). The UTC time is
the host's clock when the fix is published.
