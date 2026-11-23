# Orbit

Custom firmware for the Waveshare ESP32-S3-Touch-AMOLED-1.75C: a watch, music
remote, macro pad, phone companion, map, LUMIX remote, photo frame, virtual
pet, voice assistant and pocket synth, laid out like Wear OS. It also has a
few things just for fun: liquid ink you can stir, a Genshin-style Vision and
a Zenless Zone Zero companion.

Built on ESP-IDF 5.5 and LVGL 9.5.

## Getting around

| Do this | And you get |
| --- | --- |
| Swipe down from the watch face | Quick settings (it scrolls) |
| Swipe up | Notifications. Swipe a card sideways to dismiss, tap to open it |
| Swipe left or right | Tiles. Choose and order them in Settings > Tiles |
| Press the side (BOOT) button | App list, recent apps first. Press again to go home |
| Double-press the side button | The app you used before this one |
| Hold the side button | Assistant, already listening |
| Drag right inside an app | The app slides away under your finger |
| Press the power button | Back, then home, then rest the display |
| Hold the power button | Power menu |
| Hold a finger on the watch face | Watch face picker |

A running timer, stopwatch, song or camera link shows as a chip at the bottom
of the watch face; tap it to jump back in. When the display rests, the face
you were looking at dims rather than being replaced.

## Apps

| Group | Apps |
| --- | --- |
| Clock | Alarm, Stopwatch, Timer, Focus, World Clock, Calendar |
| Health | Fitness, Workout, Water, Relax |
| Music | Music, Synth, Metronome, Tuner |
| Photography | Camera (with time-lapse), Photos, Sun Times, Exposure, Moon |
| Tools | Calculator, Counter, Dice, Flashlight (hold for SOS), Level, Find Phone |
| Connected | Assistant, Phone, Agenda, Keys, Presenter, Maps, Weather |
| Fun | Pet, Reflex, Marble, Fluid, Vision, ZZZ |

There are five watch faces: Digital, Analog, Photo, Vision and TV wall. Hold a
finger on the face to choose. Each has a dimmer version that the always-on
display uses, so whichever face you pick is also what the watch shows at rest.

### Fluid, Vision and ZZZ

- **Fluid** is coloured ink in water. Drag to stir it, tilt the watch to pour
  it, or leave it alone and it stirs itself. The drop button changes the
  colours. The display stays on while it is open.
- **Vision** turns the round screen into a glowing elemental gem in the style
  of Genshin Impact. Tap the left or right side to change the element (seven
  of them), tap the region name to change the metal frame (seven regions),
  and press the clock button to make it your watch face. At rest it dims and
  holds still.
- **ZZZ** keeps track of your Battery Charge in Zenless Zone Zero. Use - and +
  once to match the number in the game (hold for steps of ten), then tap -20,
  -40 or -60 whenever you spend some. The watch counts it back up, one point
  every six minutes, shows when it will be full, and tells you when it is
  (not during Do not disturb or bedtime). The four round buttons are the daily
  errands (coffee, scratch card, video store, daily 400); they clear at 4 am.
  The TV button switches on the TV wall watch face, which shows the time on
  four tube TVs with your charge underneath.

The Vision emblems and frames and the TV wall are drawn from scratch for this
watch in the spirit of the games. They are not the games' own artwork.

## Build and flash

```powershell
.\tools\idf.ps1 build
.\tools\idf.ps1 -p COM5 flash monitor
```

Replace `COM5` with the port the watch appears on. If flashing does not start,
hold BOOT while plugging the USB cable in. The first build takes about ten
minutes. The build folder lives in `%USERPROFILE%\.orbit-build`, outside
OneDrive, because OneDrive locks files mid-build.

After the first flash, later versions can go over Wi-Fi: on the watch press
Settings > System > Allow update, then upload `orbit.bin` from the build folder
on the setup page. If the new version fails to start, the watch falls back to
the previous one.

## First-time setup

The first start walks through setup on the watch itself: Wi-Fi (pick a network
and type its password on the watch, or use your phone), time zone, pairing
your phone, and choosing a colour and watch face. It can be run again from
Settings > System.

Everything on the watch is adjustable in Settings, grouped as Connectivity,
Display, Sound & alerts, Battery, Apps, Date & time and System.

A few things are set from a phone or computer instead, on the watch's own
setup page, because they involve files or long text:

1. If the watch is on Wi-Fi, open `http://orbit.local`, or scan the QR code
   under Settings > Connectivity > Set up from phone.
2. If it is not, that same screen starts an `Orbit-Setup` hotspot; scan its QR
   code and the page opens by itself (or go to `192.168.4.1`).

The setup page is where you add photos, edit the Keys buttons, save your
OpenAI key, save the camera's Wi-Fi details, download maps for offline use and
install updates. Photos travel over Wi-Fi from that page; the browser crops
and shrinks them before upload.

### Phone

- **iPhone**: Settings > Bluetooth > Orbit, and accept the pairing and
  notification prompts. No app needed. You get notifications, calls, now
  playing with controls, and the time.
- **Android**: install Gadgetbridge, add a device, and choose the type
  "Bangle.js" for Orbit (long-press it in the discovery list if it is not
  recognised by name). You get notifications with quick replies, calls,
  music, weather, find my phone, and your phone's GPS for Maps.
- **Android, with the Orbit app** (in `companion/`, built separately): the
  same as Gadgetbridge, plus a paired, encrypted link; the track's picture
  behind the Music screen; shuffle, repeat, like and seek where the player
  allows them (hold previous or next to skip back 10 s or forward 30 s); and
  the assistant answered through the phone, so it works away from Wi-Fi and
  without a key stored on the watch. It also brings your calendar (the Agenda
  app and tile), photos sent over Bluetooth instead of the setup page, the
  watch's main settings changed from the phone, a step history kept on the
  phone, your own quick replies, the phone's battery in the Phone app, and
  the phone's rough position so Sun Times works without opening Maps. How
  the two talk is written down in `companion/PROTOCOL.md`.

### Keys, Presenter and MIDI

The watch is one Bluetooth device at a time. Opening Keys or Presenter turns
it into a keyboard called "Orbit Keys"; switching on Bluetooth MIDI in the
Synth's settings turns it into an instrument called "Orbit MIDI". Pair each
once on the computer or tablet. The phone link is paused while either is on.

As a MIDI device the Synth's pads send notes, tilting the watch sends
controller 74, and notes sent to the watch play its own synth.

### Camera

On the LUMIX choose Wi-Fi > New Connection > Remote Shooting & View > Direct,
enter the network name and password it shows on the setup page, then press
Connect in the Camera app. The watch leaves your home Wi-Fi while connected.

## What has and has not been tested

Every screen has been rendered and checked in the desktop simulator, and the
firmware compiles. Nothing has run on the real watch yet. Expect the first
flash to need fixes, most likely in this order:

- **Bluetooth** (iPhone services, Gadgetbridge, keyboard and MIDI modes):
  written from the published specifications, never run against real devices.
- **Orbit phone app link** (pairing, album art, assistant through the phone):
  written against `companion/PROTOCOL.md`. The watch side and the app have
  each been built on their own and have never been connected to each other.
- **Sound** (synth, metronome, tuner, assistant): the synth's response time
  depends on audio buffering that can only be tuned on the watch.
- **Motion** (level, tilt filter, raise to wake, steps): which way the sensor
  is mounted has to be checked on the watch; directions may need flipping.
- **LUMIX remote**: written from the publicly documented Wi-Fi protocol. Aimed
  at the S5. The S1 II uses the newer LUMIX Lab app and may differ.
- **Assistant**: uses three OpenAI calls (speech to text, chat, text to
  speech). Model names were current in November 2026 and can be changed on the
  setup page.
- **Battery life**: the display rests, but the chip does not light-sleep yet,
  so expect hours rather than days.
- **Memory**: the firmware fits its 4 MB slot and the 8 MB external RAM has
  room to spare, but the chip's internal RAM is tight with Wi-Fi and
  Bluetooth both on. Settings > About shows what is free. If a feature fails
  to start, the boot log says which.
- **Frame rate**: the display link tops out near 46 full frames a second, so
  taps and small animations are smooth while full-screen swipes run at about
  25 to 35 fps.
- **Fluid and Vision**: both repaint the whole screen by hand. How fast Fluid
  runs on the watch is a guess until it is flashed; if it is sluggish, the
  grid size and solver passes are two numbers at the top of
  `main/apps/app_fluid.c`. The Vision face repaints about twelve times a
  second while you look at it, which costs some battery compared with the
  plain faces.
- **World Clock**: summer time is worked out from the rules for Europe, North
  America, Australia and New Zealand as they stand in 2026.
- **Haptics**: the board has no vibration motor, so alerts are sound only.
- **iPhone limits**: iOS does not share GPS, weather or find-my-phone with
  accessories, so those need Android.

Serial logs (`monitor`) name each chip found at boot and each failed step.

## Simulator

```powershell
.\tools\sim.ps1          # render every screen to sim\out\*.png
.\tools\sim.ps1 pet      # only screens whose name contains "pet"
```

Needs MSYS2's UCRT64 gcc, cmake and ninja. The simulator compiles the real
UI code from `main/core`, `main/ui` and `main/apps` against stand-in hardware.

## Layout

```
main/core      state, settings, event bus, synth engine (portable)
main/ui        look and feel, shell, watch faces, home tiles, the
               hand-painted full-screen pictures (portable)
main/apps      one file per group of apps (portable)
main/platform  the watch's hardware: board, audio, Wi-Fi, web page, Bluetooth,
               map tiles, camera, assistant
main/web       the setup page
sim/           desktop simulator
tools/         build wrapper, icon and font generators
```

Icons and the large clock numerals are generated by `tools/gen_icons.py` and
`tools/gen_fonts.py`. Montserrat is used under the SIL Open Font License
(`assets/fonts/OFL.txt`). Map data is from OpenStreetMap contributors.
