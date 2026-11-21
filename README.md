# Pixel 8 CYD UI (LVGL) — v1.0

A Pixel 8-inspired Material 3 (Material You) Android UI firmware for the **ESP32 Cheap Yellow Display (CYD / ESP32-2432S028)** built with **LVGL 8.4**, **TFT_eSPI**, **NimBLE-Arduino**, and **PlatformIO / Arduino**.

## Features
- **Modular Single-File App Architecture**: Drop a single `src/apps/app_<name>.cpp` file with `REGISTER_APP(...)` / `REGISTER_APP_THEMED(...)` / `REGISTER_APP_EX(...)` and it automatically registers, sorts, and paginates onto the Home Screen (up to 32 apps across dynamic 4×2 pages) with zero edits to core UI files.
- **240×320 Portrait Material 3 UI**: Uses ESP32 dynamic heap (`LV_MEM_CUSTOM 1`) with live **Dark**, **Light**, and **AMOLED Black** theme switching across Home, Status Bar, Nav Bar, Quick Settings, Keyboard, and active apps.
- **Compacted Home Screen**: Single-line At-a-Glance date + inline weather icon & temperature, enlarged `56×70` (`52×52` icon) vibrant solid-background app tiles with crisp white icons (plus custom 52×52 WhatsApp icon on Page 1) on a swipeable 4×2 grid with dynamic page dots, and Google Search pill.
- **Clean 3-Button Android Bottom Bar & Tall Menu Popup**: Transparent bottom bar with Back, Home, and Menu buttons plus an enlarged `184×226` popup menu (`38px` tappable rows).
- **Modular Reusable Modal (`m3_modal`)**: Centered `3/4 × 2/3` Material 3 dialog with rounded corners, drop shadow, dimmed backdrop, and top-right `X` close button supporting both simple action-button modals and multi-step `Prev`/`Next`/`Done` walkthroughs.
- **Slide-to-Select On-Screen Keyboard**: 4-row QWERTY / `123` symbols keyboard with a live floating key-preview popup that tracks your finger as you drag across keys to switch letters before releasing, drag-off cancel, and Light/Dark theming.
- **Tuned Resistive Touch**: `TOUCH_Z_MIN = 80` with a 2-sample coordinate-delta noise filter (`TOUCH_SAMPLE_MAX_DELTA = 120`) for reliable light finger taps without phantom spikes.
- **Real ESP32 Hardware Features**:
  - **Wi-Fi (`app_wifi.cpp`, `wifi_mgr`)**: Async 2.4 GHz `WiFi.scanNetworks()`, on-screen keyboard password entry, `WiFi.begin()`, NVS credential persistence & boot auto-reconnect, live RSSI bars, and Airplane Mode support.
  - **NTP Clock & Date (`app_clock.cpp`, `time_mgr`)**: Automatic `configTime()` sync over Wi-Fi, persisted UTC timezone offset in NVS (`-12:00..+14:00`), live `lv_calendar`, and synchronized time/date in Status Bar, Home Screen, and Quick Settings.
  - **HTTP/HTTPS Text Browser (`app_browser.cpp`, `http_reader`)**: Async FreeRTOS worker fetching real web pages over Wi-Fi (`HTTPClient` / `WiFiClientSecure`) with redirect following, streaming HTML-to-text extraction, and working bookmarks.
  - **Persistent Notes (`app_notes.cpp`, `notes_store`)**: Up to 8 notes persisted in ESP32 NVS (`Preferences`) across reboots, plus optional hot-plug microSD export/import (`/pixel_notes.txt` on VSPI).
  - **Bluetooth BLE Scanner (`app_bluetooth.cpp`, `ble_mgr`)**: Scanner-only `NimBLE-Arduino` integration displaying nearby BLE device names, uppercase MAC addresses, and RSSI with lazy init for heap efficiency.
  - **WhatsApp Companion (`app_whatsapp.cpp`, `companion_mgr`)**: Local-Wi-Fi RFC 6455 WebSocket bridge to the Android companion app (`../android-companion`) for live WhatsApp notification previews, unread badge, quick replies via Android `RemoteInput`, notification dismissal, and a built-in `?` multi-step setup guide modal.
- **Visual-Limits Benchmark Apps**:
  - **SqLine Lab (`app_sqline_limits.cpp`)**: SquareLine Studio visual-limits showcase (gradients, dual radial arcs, glassmorphic alpha slider, animated line chart, font scaling, 12-widget Stress Arena, live FPS & free-heap HUD).
  - **EEZ Studio (`app_eez_limits.cpp`)**: EEZ Studio HMI visual-limits showcase (instrument gauge + live channel bars, Bars/Line chart toggle, alpha/shadow depth deck, `A-`/`A+` font ladder, 16-widget Stress Matrix, live FPS & free-heap HUD).
- **17 Registered Apps (3 dynamic 4×2 pages)**: Files, Gallery, Terminal, Wi-Fi, Bluetooth, WhatsApp, Calc, About, Browser, Music, Notes, Settings, Theme, Clock, SqLine Lab, EEZ Studio, and Sys Info.

## Hardware
- **Boards**:
  - `cyd2usb`: Dual-USB / USB-C CYD (`ST7789`, `TFT_INVERSION_OFF`, `GPIO 27` backlight) — default environment
  - `cyd`: Classic single-USB CYD (`ILI9341`, `GPIO 21` backlight)
- **Touch**: XPT2046 resistive touch on dedicated pins (`CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36`) with portrait X-mirror correction, `TOUCH_Z_MIN=80`, and 2-sample delta filtering (`TOUCH_SAMPLE_MAX_DELTA=120`).
- **microSD (optional)**: VSPI (`SCK=18, MISO=19, MOSI=23, CS=5`) used on-demand by Notes export/import.

## Memory & Flash Footprint (`huge_app.csv`, 3 MB app partition)
| Target | RAM (Static) | Flash Used | Flash Free |
| --- | --- | --- | --- |
| `cyd2usb` (ST7789) | ~31.0% (~101.7 KB / 320 KB) | ~52.1% (~1.64 MB / 3.15 MB) | ~47.9% (~1.51 MB) |
| `cyd` (ILI9341) | ~31.0% (~101.7 KB / 320 KB) | ~52.1% (~1.64 MB / 3.15 MB) | ~47.9% (~1.51 MB) |

## Build & Flash
```bash
# Build both board targets
pio run -e cyd2usb -e cyd

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
- **v1.0**: Modular single-file `REGISTER_APP` architecture with dynamic Home pagination, tuned light-touch sensitivity (`TOUCH_Z_MIN=80` + 2-sample filter), live active-app theme refresh, compacted At-a-Glance + larger app tiles, clean transparent nav bar + taller Menu popup, iPhone-style keyboard key-preview popup, real 2.4 GHz Wi-Fi + NVS, real NTP clock/date + timezone, real HTTP/HTTPS text reader browser, real NVS + microSD Notes, real NimBLE BLE scanner, and SquareLine Studio + EEZ Studio visual-limits benchmark apps.
- **v0.2**: Persistent 3-button Android bottom bar (Back/Home/Menu), Dark/Light Theme app icon, custom Light/Dark on-screen keyboard, ST7789 touch X-mirror fix & light-touch tuning, and `LV_MEM_CUSTOM 1` dynamic heap allocation.
- **v0.1**: Initial Pixel 8 CYD LVGL UI release.
