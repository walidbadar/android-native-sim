---
title: Sound
nav_order: 5
---

# Sound

Android keeps `/dev/snd` from Debian, so an application that plays through ALSA,
such as one using `CONFIG_I2S_NATIVE_SIM_ALSA`, reaches the speaker through
PulseAudio in Termux. This covers playback only.

This builds on the Termux and Debian setup from
[Applications with a display](display.md). Set up the phone once, in Termux:

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
