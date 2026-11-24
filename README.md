<p align="center">
  <img src="assets/logo/orbit-lockup.svg" alt="Orbit" width="280">
</p>

<p align="center">
  <b>A Wear OS–inspired smartwatch firmware and Android companion app for the Waveshare ESP32-S3-Touch-AMOLED-1.75C.</b>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/ESP--IDF-5.5+-blue" alt="ESP-IDF 5.5+">
  <img src="https://img.shields.io/badge/LVGL-9.5-brightgreen" alt="LVGL 9.5">
  <img src="https://img.shields.io/badge/Hardware-Waveshare%20AMOLED%201.75C-orange" alt="Waveshare ESP32-S3-Touch-AMOLED-1.75C">
  <img src="https://img.shields.io/badge/License-MIT-lightgrey" alt="MIT License">
</p>

---

Orbit brings a smooth, Material You–styled Wear OS interface to the round 466×466 AMOLED **Waveshare ESP32-S3-Touch-AMOLED-1.75C** — complete with Control Centre, Always-on Display, 8 watch faces (including a web-based custom watch face builder), 30+ built-in apps, 80% battery health charge limiting, Wi-Fi OTA updates with automatic rollback, and a dedicated Android companion app.

## Highlights

- **Wear OS navigation**: Swipe down for Control Centre, up for notifications, sideways for customizable tiles, side button for the curved app launcher, swipe right inside any app to dismiss.
- **Control Centre**: Instant brightness & volume sliders at the top, one-tap **Sleep**, Always-on Display cycling, Do Not Disturb, Flashlight, Battery Saver, Bedtime, Theatre mode, Touch Lock, Find Phone, BLE Keys, and Settings.
- **Always-on Display (AoD)**: Dims whichever watch face you're using (or cycles your photos) with ±10 px minute-by-minute OLED burn-in shifting and a full-bezel charging ring on USB power.
- **6 Watch Faces + Custom Face Builder**: *Digital*, *Analog*, *Modular* (agenda/weather/pet card), *Photo*, *Minimal* (Bauhaus dial), and *Custom* (create up to 6 custom faces over Wi-Fi at `http://orbit.local`).
- **Battery Health**: Optional **80% Charge Limit** in *Settings > Battery* enforced directly via the AXP2101 PMIC (`CELL_BATT_CHG_EN`, 75% resume hysteresis).
- **Wi-Fi OTA with Rollback**: Dual 4 MB OTA slots with a 5-minute physical approval window (*Settings > System > Allow update*), live upload progress bar, and automatic bootloader rollback if a bad build fails to start.
- **Phone & Computer Connectivity**:
  - **Android (Orbit Companion App)**: Encrypted BLE link with notifications + quick replies, calls, full media controls + album art, calendar/agenda, weather, GPS, photo sync, remote settings, step history, and phone-relayed voice assistant.
  - **Android (Gadgetbridge)**: Works out of the box as a *Bangle.js* device.
  - **iPhone**: Native ANCS/AMS/CTS support — notifications, calls, media controls, and clock sync with no app required.
  - **Bluetooth HID & MIDI**: Switch into a wireless macro pad (*Orbit Keys*), presentation remote, or BLE MIDI instrument (*Orbit MIDI*).
- **Smooth Hardware Performance**: Internal DMA double buffering (2 × 24 lines) pinned to Core 1 with LVGL objects in 8 MB PSRAM, keeping Core 0 dedicated to Wi-Fi and BLE.

## Getting Around

| Gesture / Button | Action |
| --- | --- |
| Swipe down from watch face | Control Centre (brightness, volume, Sleep, AoD, quick toggles) |
| Swipe up | Notifications (swipe card sideways to dismiss, tap to open) |
| Swipe left or right | Tiles (configure in *Settings > Tiles*) |
| Press side (BOOT) button | App launcher (recent apps first); press again to go home |
| Double-press side button | Switch to previous app |
| Hold side button | Voice assistant (starts listening immediately) |
| Drag right inside an app | Swipe-to-dismiss back to home |
| Press power button | Back → home → rest display (AoD / sleep) |
| Hold power button | Power menu (Off / Restart) |
| Long-press watch face | Watch face picker |

## Built-in Apps

| Group | Apps |
| --- | --- |
| **Clock** | Alarm, Stopwatch, Timer, World Clock, Calendar |
| **Health** | Fitness, Workout |
| **Music** | Music Remote |
| **Photography** | LUMIX Wi-Fi Camera Remote (with time-lapse), Photos, Sun Times, Exposure, Moon |
| **Tools** | Calculator, Flashlight (hold for SOS), Find Phone |
| **Connected** | Voice Assistant, Phone, Agenda, Macro Keys, Presenter, Offline Maps, Weather |
| **Fun** | Virtual Pet |

## Quick Install (Prebuilt Releases)

Download the latest release assets from the [**Releases**](https://github.com/AnriMnwh/Orbit/releases) page:

1. **First-time USB flash** (requires [esptool](https://docs.espressif.com/projects/esptool/)):
   ```bash
   esptool --chip esp32s3 -p COM5 -b 460800 write_flash 0x0 orbit-v0.1.0-factory.bin
   ```
   *(Replace `COM5` with your watch's serial port, e.g. `/dev/ttyACM0` on Linux/macOS. Hold BOOT while plugging in USB if flashing doesn't start automatically.)*

2. **Subsequent Wi-Fi OTA updates** (no USB cable needed):
   - On the watch: **Settings > System > Allow update**
   - In your browser on the same Wi-Fi network: open `http://orbit.local` → **Update** → upload `orbit-v0.1.0-ota.bin`

3. **Android companion app** (optional):
   - Install `orbit-companion-v0.1.0.apk` on Android 13+ (API 33+), grant Bluetooth and notification access, and tap **Pair watch**.

## First-Time Setup & Web Setup Page

On first boot, the watch walks you through Wi-Fi, time zone, phone pairing, accent colour, and watch face selection (you can re-run it anytime under *Settings > System*).

For photos, custom watch faces, macro keys, OpenAI keys, LUMIX Wi-Fi credentials, offline map tiles, and OTA updates, open the watch's built-in setup page:
- **On Wi-Fi**: open `http://orbit.local` (or the IP shown in *Settings > Connectivity*).
- **No Wi-Fi yet**: open *Settings > Connectivity > Set up from phone* to start the `Orbit-Setup` hotspot and scan its QR code (or browse to `http://192.168.4.1`).

## Building from Source

### Watch Firmware (ESP-IDF 5.5+)

```powershell
# Windows (PowerShell wrapper auto-detects ESP-IDF 5.5.x under C:\Espressif)
.\tools\idf.ps1 build
.\tools\idf.ps1 -p COM5 flash monitor
```

Or with standard `idf.py` on Windows/Linux/macOS:
```bash
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

### Android Companion App

Requires JDK 21 and the Android SDK (compileSdk 37):
```bash
cd companion/android
./gradlew :app:testDebugUnitTest :app:assembleDebug
```

### Desktop Simulator

Compiles `main/core`, `main/ui`, and `main/apps` natively with LVGL against mock hardware (requires MSYS2 UCRT64 `gcc`, `cmake`, and `ninja` on Windows):
```powershell
.\tools\sim.ps1          # render all screens to sim/out/*.png
.\tools\sim.ps1 quick    # render screens matching "quick"
```

## Hardware Status & Notes

- **Verified on hardware**: CO5300 QSPI AMOLED display (466×466), CST9217 touch, AXP2101 PMIC (battery, power button, 80% charge limit), QMI8658 IMU, ES8311 speaker + ES7210 dual mics, Wi-Fi, mDNS (`orbit.local`), BLE NimBLE advertising, and internal DMA double buffering (~46 KB free internal RAM + ~8 MB free PSRAM at boot).
- **Power**: CPU frequency scaling (80–240 MHz) and display rest/AoD are active; deep/light sleep with radio wake is planned for a future release.
- **Haptics**: The Waveshare 1.75C board has no vibration motor, so alerts use the onboard speaker.
- **Security**: The watch web server enforces same-origin checks on state-changing API endpoints, sets strict CSP/security headers, zeroes secret buffers after use, and gates OTA behind a 5-minute physical approval window.

## Project Layout

```text
main/core       Portable state, settings, event bus, synth engine
main/ui         Theme, shell, watch faces, Control Centre, tiles
main/apps       Built-in apps (one file per app group)
main/platform   ESP32-S3 hardware layer (board, audio, Wi-Fi, web, BLE, OTA, maps, camera, AI)
main/web        Embedded web setup & OTA page
companion/      Android companion app (Kotlin + Jetpack Compose) & BLE protocol spec
sim/            Desktop LVGL simulator
tools/          Build wrappers and icon/font/logo generators
assets/         Fonts (Montserrat, SIL OFL) and Orbit vector/PNG branding
```

## License & Credits

- Licensed under the [MIT License](LICENSE).
- Montserrat font used under the [SIL Open Font License](assets/fonts/OFL.txt).
- Offline map tiles use data from OpenStreetMap contributors.
