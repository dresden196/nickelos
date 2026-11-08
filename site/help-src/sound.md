title: Sound, camera and screen sharing
nav: Sound and camera
order: 60
summary: How sound gets from a Linux app to your speakers on NickelOS (PipeWire, pipewire-pulse, WirePlumber and CRAS), microphones, webcams through PipeWire and the Camera portal, and screen sharing and remote control.
---
# Sound, camera and screen sharing

Linux apps' sound and microphones go through ChromeOS's own sound server on NickelOS, and webcams and screen sharing go through PipeWire with ChromeOS's dialogs. Here's how each one is wired up, and how to check it.

## How sound gets out

ChromeOS has its own sound server, **CRAS**. It owns the sound cards and Bluetooth audio, and it's what the volume keys, the output picker in Quick Settings and Chrome's tabs all use. NickelOS keeps it, so Linux apps play into CRAS too. That way one volume and one output choice covers everything.

Linux apps don't speak CRAS, though. They speak PulseAudio or PipeWire. So every session runs a small PipeWire setup that hands their sound over:

| Piece | Job | Runs as |
|---|---|---|
| **PipeWire** | The audio graph in your session | `nickel-pipewire` |
| **pipewire-pulse** | A PulseAudio server, for apps that speak PulseAudio (most of them, every Flatpak with sound included) | `nickel-audio` |
| **WirePlumber** | Links apps' streams, keeps the defaults and volumes | `nickel-wireplumber` |
| **cras_out**, **cras_in** | Two PipeWire nodes that play into and record from CRAS | Defined in PipeWire's configuration (`nickel-cras.conf`) |

So the path for sound is:

```
PulseAudio apps ─► pipewire-pulse ─┐
                                   ├─► PipeWire ─► cras_out ─► CRAS ─► speakers
PipeWire apps ─────────────────────┘
```

`cras_out` plays into ALSA's `default` device, which on ChromeOS is CRAS's own ALSA plugin.

The microphone goes the other way, from CRAS through `cras_in`. Both nodes run at 48 kHz, 16-bit stereo, which is what CRAS takes. WirePlumber pauses them when nothing is playing, so CRAS only sees a stream while an app is actually making sound. Apps that use ALSA directly reach CRAS through the same plugin.

WirePlumber is set up to leave the sound hardware alone (`hardware.audio` and `hardware.bluetooth` are off). CRAS is in charge of the sound cards, and PipeWire doesn't fight it for them.

### Checking it

```
pactl info              # should say "PulseAudio (on PipeWire ...)", default sink cras_out
wpctl status -n         # PipeWire's view by node name: cras_out, cras_in and the apps' streams
pactl list short sinks
```

If an app is silent, check `wpctl status -n` while it plays. Its stream should be listed, with `cras_out` as the default sink (without `-n`, both nodes show up by their description, "CRAS"). Then check the output device and volume in Quick Settings, because that's CRAS, and it decides where everything goes.

### AMD laptops

Many AMD laptops have a sound codec that shows up as "HD-Audio Generic". ChromiumOS's generic configuration for that codec only described a microphone, so CRAS left the speaker muted. NickelOS skips that configuration for those cards, which lets CRAS find Master, Speaker and Headphone itself and drive them.

## Microphones

Microphones are CRAS's too. Apps record from `cras_in`, and you pick the input device in Quick Settings, like the output.

## Webcams

Linux apps get the camera through PipeWire, the way current desktop apps expect:

- PipeWire has its V4L2 support, and WirePlumber turns each webcam into a camera node.
- Session users are in group `camera`, so their PipeWire can open the webcam. (On ChromeOS only its camera service can.)
- The first time an app asks for the camera through the Camera portal, ChromeOS asks: "Allow (app) to use the camera?" The answer is remembered for that app.
- If camera access is off in **Settings > Privacy controls**, the dialog says so and offers the setting instead.

Only one program can stream from a webcam at a time. If the Camera app is open, close it before you use the camera in another app.

## Screen sharing

Apps that share or record the screen ask for it through the ScreenCast portal, the same as on GNOME and KDE:

1. ChromeOS's "share your screen" picker opens, with the app's name. You pick the whole screen or one window.
2. NickelOS's portal backend publishes it as a PipeWire stream, at up to 30 frames a second, and links the app to it.
3. While it's shared, a ChromeOS notification says so, with **Stop**.

Apps that take a screenshot through the Screenshot portal get the same picker. NickelOS takes one frame of what you picked, after the picker has gone, and saves it in `~/Pictures/Screenshots`.

## Remote control

Remote desktop apps can also control the device, through the RemoteDesktop portal:

- ChromeOS asks "Allow remote control?" first.
- Input goes in through Chrome's own input injector, the one Chrome Remote Desktop uses.
- A notification says the device is being controlled, with **Stop**.
- What the other computer copies lands on ChromeOS's clipboard, and what you copy here goes back, but only while you're letting the app control the device.
