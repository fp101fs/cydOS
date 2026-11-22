# Pixel 8 CYD UI (LVGL) — v1.1

A Pixel 8-inspired Material 3 (Material You) Android UI firmware for the **ESP32 Cheap Yellow Display (CYD / ESP32-2432S028)** built with **LVGL 8.4**, **TFT_eSPI**, **NimBLE-Arduino**, **bmorcelli/Launcher** dynamic partition/OTA backend, and **PlatformIO / Arduino**.

## Features
- **Modular Single-File App Architecture + Dynamic Runtime Apps**: Drop a single `src/apps/app_<name>.cpp` file with `REGISTER_APP(...)` / `REGISTER_APP_THEMED(...)` / `REGISTER_APP_EX(...)` and it automatically registers, sorts, and paginates onto the Home Screen (up to 40 total slots: built-in apps + up to 16 dynamically installed Launcher Apps across dynamic 4×2 pages) with zero edits to core UI files.
- **Material 3 App Store (`app_store.cpp`, `src/launcher/`)**: Reskins [`bmorcelli/Launcher`](https://github.com/bmorcelli/Launcher) using its exact dynamic 4 MB partition manager, NVS registry (`"l_apps"`), LauncherHub OTA downloader/installer, merged-binary/SPIFFS/FAT/LittleFS flasher, SD card manager, data backup/restore, and local WebUI behind a native Pixel 8 Material 3 interface:
  - **Home Screen Integration**: **App Store** lives at position #1 (`order = 0`), and every newly installed App is automatically inserted at **Home Screen slot #1** (newest first, right next to App Store) with tap-to-boot and long-press actions (`Launch`, `Rename`, `Uninstall`, `Open in App Store`).
  - **Explore Tab**: Browse LauncherHub by `Popular`, `Recent`, `A-Z`, or `Starred`, search with `m3_keyboard`, inspect App details & version picker, install directly to flash or download to `/downloads/` on SD, and **Install from URL**.
  - **SD Card Tab**: Auto-mounts VSPI microSD, defaults to `/downloads/`, supports `.bin` filtering, folder navigation, copy/paste, rename, new folder, delete, and narrated SD-to-flash App installs.
  - **Installed Tab**: Live dynamic flash usage bar, newest-first App cards synced with the Home Screen, one-tap **Run**, **Rename**, SD **Backup/Restore**, and **Remove**.
  - **Tools Tab**:
    - **Storage & Partitions (`PMan`)**: Visual color-coded flash map, fragmentation check & flash compaction, partition table dump, and per-partition dump/restore/delete.
    - **Backup & Restore**: One-tap backup of all installed Apps' SPIFFS/FAT/LittleFS data partitions to `/bkp/` on SD and per-App restore.
    - **Check for App Updates**: Checks `/downloads/downloaded.json` against LauncherHub and opens available updates in **Explore**.
    - **Web Upload (WUI)**: Local `esp_http_server` on current Wi-Fi or one-tap **`Pixel-CYD-Store`** Hotspot (AP) with scannable `lv_qrcode`, live upload/flash progress, and automatic Home Screen sync.
    - **Store Settings**: Boot target preference (`Pixel CYD` vs `Last Launched App`), active boot slot reset, auto-backup toggle, LauncherHub device tag (`CYD` / `CYD-2USB` / `ESP32` / custom), and NVS App Registry clear.
- **240×320 Portrait Material 3 UI**: Uses ESP32 dynamic heap (`LV_MEM_CUSTOM 1`) with live **Dark**, **Light**, and **AMOLED Black** theme switching across Home, Status Bar, Nav Bar, Quick Settings, Keyboard, App Store, and active apps.
- **Compacted Home Screen**: Single-line At-a-Glance date + inline weather icon & temperature, enlarged `56×70` (`52×52` icon) vibrant solid-background app tiles with crisp white icons (plus custom 52×52 WhatsApp icon and accent badge on dynamic Launcher Apps) on a swipeable 4×2 grid with dynamic page dots, and Google Search pill.
- **Clean 3-Button Android Bottom Bar & Tall Menu Popup**: Slim `38 px` transparent bottom bar with Back, Home, and Menu buttons plus an enlarged `184×226` popup menu (`38px` tappable rows).
- **Modular Reusable Modal (`m3_modal`)**: Centered `3/4 × 2/3` Material 3 dialog with rounded corners, drop shadow, dimmed backdrop, and top-right `X` close button supporting action-button modals, multi-step `Prev`/`Next`/`Done` walkthroughs, and real-time animated indeterminate/determinate progress bars (`m3ModalShowProgress` / `m3ModalUpdateProgress` / `m3ModalFinishProgress`) with plain-English step narration and locked-commit protection (blocking nav bar & physical BOOT button during critical flash commits).
- **Slide-to-Select On-Screen Keyboard**: 4-row QWERTY / `123` symbols keyboard with a live floating key-preview popup that tracks your finger as you drag across keys to switch letters before releasing, drag-off cancel, and Light/Dark theming.
- **Tuned Resistive Touch**: `TOUCH_Z_MIN = 80` with a 2-sample coordinate-delta noise filter (`TOUCH_SAMPLE_MAX_DELTA = 120`) for reliable light finger taps without phantom spikes.
- **Real ESP32 Hardware Features**:
  - **Wi-Fi (`app_wifi.cpp`, `wifi_mgr`)**: Async 2.4 GHz `WiFi.scanNetworks()`, on-screen keyboard password entry, `WiFi.begin()`, NVS credential persistence & boot auto-reconnect, live RSSI bars, and Airplane Mode support.
  - **NTP Clock & Date (`app_clock.cpp`, `time_mgr`)**: Automatic `configTime()` sync over Wi-Fi, persisted UTC timezone offset in NVS (`-12:00..+14:00`), live `lv_calendar`, and synchronized time/date in Status Bar, Home Screen, and Quick Settings.
  - **HTTP/HTTPS Text Browser (`app_browser.cpp`, `http_reader`)**: Async FreeRTOS worker fetching real web pages over Wi-Fi (`HTTPClient` / `WiFiClientSecure`) with redirect following, streaming HTML-to-text extraction, and working bookmarks.
  - **Persistent Notes (`app_notes.cpp`, `notes_store`)**: Up to 8 notes persisted in ESP32 NVS (`Preferences`) across reboots, plus optional hot-plug microSD export/import (`/pixel_notes.txt` on VSPI).
  - **Files & Micro SD Browser (`app_files.cpp`)**: Internal storage browser plus an on-demand **Micro SD Card** browser (VSPI) with card capacity readout, subfolder navigation, file size inspection, and a triple-confirm (`m3_modal`) **FAT32 Format** button (`f_mkfs`).
  - **Bluetooth BLE Scanner (`app_bluetooth.cpp`, `ble_mgr`)**: Scanner-only `NimBLE-Arduino` integration displaying nearby BLE device names, uppercase MAC addresses, and RSSI with lazy init for heap efficiency.
  - **WhatsApp Companion (`app_whatsapp.cpp`, `companion_mgr`)**: Local-Wi-Fi RFC 6455 WebSocket bridge to the Android companion app (`android-companion/`) with a WhatsApp Android-style **Chat List** (24×24 profile avatars streamed from Android with circular green WhatsApp icon fallback, unread count badges, timestamps) and **Conversation View** (WhatsApp chat header, wallpaper, incoming/outgoing chat bubbles with blue checkmarks, rounded message composer + circular send button), NVS flash persistence across reboots, quick replies via Android `RemoteInput`, notification dismissal, Ping/Pong keepalive, and a black `?` setup guide button in the right side of the app title bar.
  - **Android Link (`app_android_link.cpp`)**: One-tap QR setup app using LVGL `lv_qrcode` — Step 1 shows a direct APK download QR (`releases/latest/download/pixel-cyd-companion.apk`) with an install-help `?` modal; Step 2 starts a temporary local pairing endpoint (`:8766`) and displays a Pairing QR scanned directly inside the Android Companion app for zero-typing setup.
  - **Bluetooth Trackpad & Fire TV Media Remote (`app_trackpad.cpp`, `ble_hid_mgr`)**: Composite BLE HID (`Pixel CYD Remote`) standalone app with low-latency `7.5–15 ms` BLE connection params, automatic Wi-Fi DRAM handoff when heap is tight, **Pad Mode** (feather-light `PENIRQ` touch with 0 ms instant drag, 100 Hz EMA smoothing + spike rejection + fractional carry, 35 ms dropout grace, right-quarter velocity scroll strip, half-width Left/Right click buttons) and **Remote Mode** (classic Amazon Fire TV Stick layout with circular D-pad, OK, Back/Home/Menu, Power/Mute/Volume, and Rewind/Play-Pause/Fast-Forward).
  - **Live Screen Mirror & Programmatic Remote Control (`app_screen_mirror.cpp`, `screen_mirror`, `android-companion/`)**: Streams RLE-compressed dirty `80×108` RGB565 screen bands over the authenticated companion WebSocket (`:8765`), accepts live tap/drag/swipe touch injection + programmatic commands (`home`, `back`, `quick_settings`, `launch_app`, `tap_label`, `brightness`), and publishes a live LVGL `ui_tree` of visible interactive widgets and coordinates to the Android Companion's **CYD Screen** tab.
- **Visual-Limits Benchmark Apps**:
  - **SqLine Lab (`app_sqline_limits.cpp`)**: SquareLine Studio visual-limits showcase (gradients, dual radial arcs, glassmorphic alpha slider, animated line chart, font scaling, 12-widget Stress Arena, live FPS & free-heap HUD).
  - **EEZ Studio (`app_eez_limits.cpp`)**: EEZ Studio HMI visual-limits showcase (instrument gauge + live channel bars, Bars/Line chart toggle, alpha/shadow depth deck, `A-`/`A+` font ladder, 16-widget Stress Matrix, live FPS & free-heap HUD).
- **21 Built-in Apps + up to 16 Dynamic Installed Apps (dynamic 4×2 pages)**: App Store (slot #0), dynamic installed Launcher Apps (slots #1..N, newest first), Files, Gallery, Terminal, Wi-Fi, Bluetooth, Trackpad, Mirror, WhatsApp, Android Link, Calc, About, Browser, Music, Notes, Settings, Theme, Clock, SqLine Lab, EEZ Studio, and Sys Info.

## Hardware
- **Boards**:
  - `cyd2usb`: Dual-USB / USB-C CYD (`ST7789`, `TFT_INVERSION_OFF`, `GPIO 27` backlight) — default environment
  - `cyd`: Classic single-USB CYD (`ILI9341`, `GPIO 21` backlight)
- **Touch**: XPT2046 resistive touch on dedicated pins (`CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36`) with portrait X-mirror correction, `TOUCH_Z_MIN=80`, and 2-sample delta filtering (`TOUCH_SAMPLE_MAX_DELTA=120`).
- **microSD (optional)**: VSPI (`SCK=18, MISO=19, MOSI=23, CS=5`) used on-demand by App Store SD installs/backups, Files, and Notes export/import.

## Partition Layout & Memory Footprint (`partitions_launcher_4mb.csv`, 4 MB Flash)
- **`nvs`** (`0x009000`, 20 KB): Wi-Fi, touch calibration, timezone, notes, WhatsApp, BLE, and App Store registry (`"l_apps"`, `"l_cfg"`, `"l_bonds"`).
- **`otadata`** (`0x00E000`, 8 KB): Boot target selection between Pixel CYD (`factory`) and installed OTA Apps.
- **`factory`** (`0x010000`, 2.00 MB): Pixel CYD + App Store firmware.
- **Dynamic App Pool** (`0x210000..0x400000`, **1.94 MB / 1,984 KB**): Managed dynamically at runtime for installed Apps and their SPIFFS/FAT/LittleFS data partitions.

| Target | RAM (Static) | Runtime Free Heap (Boot) | `firmware.bin` (2.00 MB `factory` slot) | Slot Headroom | Dynamic App Pool |
| --- | --- | --- | --- | --- | --- |
| `cyd2usb` (ST7789) | 23.8% (`78.1 KB / 320 KB`) | `~79.5 KB` (`69.6 KB` max block) | 97.7% (`2,049,472 B / 2,097,152 B`) | `46.6 KB` | `1.94 MB` (`0x1F0000`) |
| `cyd` (ILI9341) | 23.8% (`78.1 KB / 320 KB`) | — | 97.7% (`~2.05 MB / 2,097,152 B`) | `~46.0 KB` | `1.94 MB` (`0x1F0000`) |

## Build, Verify & Flash
```bash
# Build both board targets
pio run -e cyd2usb -e cyd

# Verify partition table MD5, alignment, and flash headroom
python3 tools/verify_partitions.py

# Flash Dual-USB CYD (default)
pio run -e cyd2usb -t upload

# Flash Classic Single-USB CYD
pio run -e cyd -t upload
```
Hold the **BOOT** button during reset (or use **Settings → Recalibrate Touch**) to run interactive 2-point touch calibration.

## Adding a New App
Create a single file `src/apps/app_mytool.cpp`:
```cpp
#include "app_registry.h"
#include "../components/m3_widgets.h"

static void buildMyTool(lv_obj_t* body) {
    lv_obj_t* lbl = lv_label_create(body);
    lv_label_set_text(lbl, "Hello CYD!");
}

REGISTER_APP(mytool, "mytool", "My Tool", LV_SYMBOL_OK, 0x8AB4F8, 0x1D3557, buildMyTool, nullptr, 170);
```
Rebuild and flash—no other files need to be edited.

## Changelog
- **v1.1**: Added full Material 3 **App Store** (reskinned `bmorcelli/Launcher` backend) with custom 4 MB dynamic partition table (`partitions_launcher_4mb.csv`), animated narrated `m3_modal` progress dialogs, dynamic newest-first Home Screen App tiles at slot #1, LauncherHub **Explore** catalog + URL install, **SD Card** browser & `.bin` flasher, **Installed** management, **Tools** (`PMan` storage/partitions, SD backup/restore, App update checker, local Web Upload WUI with QR & Hotspot AP, and Store Settings), plus host-side unit tests and partition verifier.
- **v1.0**: Modular single-file `REGISTER_APP` architecture with dynamic Home pagination, tuned light-touch sensitivity (`TOUCH_Z_MIN=80` + 2-sample filter), live active-app theme refresh, compacted At-a-Glance + larger app tiles, clean transparent nav bar + taller Menu popup, iPhone-style keyboard key-preview popup, real 2.4 GHz Wi-Fi + NVS, real NTP clock/date + timezone, real HTTP/HTTPS text reader browser, real NVS + microSD Notes, real NimBLE BLE scanner, and SquareLine Studio + EEZ Studio visual-limits benchmark apps.
- **v0.2**: Persistent 3-button Android bottom bar (Back/Home/Menu), Dark/Light Theme app icon, custom Light/Dark on-screen keyboard, ST7789 touch X-mirror fix & light-touch tuning, and `LV_MEM_CUSTOM 1` dynamic heap allocation.
- **v0.1**: Initial Pixel 8 CYD LVGL UI release.
