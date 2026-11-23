# Orbit companion app

A phone app for the Orbit watch. It keeps the watch connected in the
background and does the work the watch is poor at: reading what is playing in
any music app, shrinking album art, and (once the watch supports it) making
the assistant's internet calls so the watch can stay off Wi-Fi.

- `PROTOCOL.md` is the contract between the app and the firmware.
- `android/` is the Android app (Kotlin, Jetpack Compose). An iPhone app comes later.

## What works today

The app talks to the watch the way Gadgetbridge does, plus extras the watch
switches on by naming them when the app says hello. Use the app or
Gadgetbridge, not both at once.

| As Gadgetbridge does | Extras, written on both sides |
| --- | --- |
| Clock, notifications with dismiss and quick reply, incoming calls | Album art, seek, shuffle, repeat, like, volume level |
| Now playing, play/pause, next, previous, volume | Assistant through the phone |
| Find my phone, find my watch, phone GPS for Maps | Photos sent over Bluetooth |
| Weather from the phone (off until switched on) | Calendar: next appointments |
| Choose which apps reach the watch; quiet while the phone is in use | Watch settings changed from the phone |
| | Step history and weekly chart |
| | Your own quick replies; phone battery on the watch |
| | Rough position for sunrise and sunset |

"Written on both sides" means both compile. None of it has run on a watch,
and the app and the watch have never talked to each other.

The app has four tabs: **Home** (status, now playing, today), **Alerts**
(which notifications reach the watch, quick replies), **Photos** (pictures for
the watch face) and **Watch** (its settings, weather, calendar, assistant,
permissions). An extra the watch does not announce shows a note in the app
saying it is waiting for newer watch software, and switches on by itself when
the watch says it supports it.

## Build and install

```powershell
cd companion\android
.\gradlew.bat :app:assembleDebug
adb install -r "$env:USERPROFILE\.orbit-build-app\app\outputs\apk\debug\app-debug.apk"
```

Build output goes to `%USERPROFILE%\.orbit-build-app`, outside OneDrive, for
the same reason the firmware's does. Needs the Android SDK (platform 37) and
JDK 21, both already on this PC.

On the phone: open Orbit, allow Bluetooth, tap **Pair watch** and pick
"Orbit", then grant notification access. The watch must be in its normal
phone mode, not Keys or MIDI.

## What has and has not been tested

- The protocol code (line format, bulk frames, speech codec, resampling,
  weather, calendar choice and lines, settings, step log) has 34 unit tests:
  `.\gradlew.bat :app:testDebugUnitTest`.
- The app builds, installs and opens on an Android 17 emulator. Checked there:
  all four tabs, the background service starting, a notification being handled
  and its app appearing in the filter list, the step chart from saved data,
  and picking photos, which are cut to 466 x 466 and queued.
- The weather service's live reply has the fields the app reads. The app's own
  weather, calendar and battery messages only go out while a watch is
  connected, so they have not been sent anywhere yet.
- **Nothing has talked to a real watch.** Pairing, the Bluetooth link, media,
  calls and the assistant are written from the Android documentation and the
  firmware's source, and are unproven until the firmware runs on hardware.
- The assistant's OpenAI model names are the firmware's defaults and have not
  been called from the app.

## Layout

```
android/app/src/main/java/com/anari/orbit/
  link/Protocol.kt     line and frame formats (no Android code; unit tested)
  link/Messages.kt     weather, calendar, settings, step log (no Android code; unit tested)
  link/Speech.kt       speech codec, WAV, resampling (no Android code; unit tested)
  link/WatchLink.kt    the Bluetooth connection and its write queues
  LinkService.kt       background service; routes messages between watch and app
  NotificationRelay.kt notifications, calls and the per-app filter
  MediaRelay.kt        now playing, controls, album art
  DailyRelay.kt        weather, calendar, rough position, phone battery, quick replies
  Photos.kt            photos cut for the watch, and their queue
  AgentRelay.kt        assistant: speech in, OpenAI, tools on the watch, speech out
  OpenAi.kt            the three OpenAI calls
  MainActivity.kt      the app's frame and shared pieces
  Screens.kt           the four tabs
```
