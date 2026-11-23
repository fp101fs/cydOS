# cydOS (`v1.1`)

A **Pixel 8 Material 3** OS for the *ESP32 Cheap Yellow Display*.

Runs on `ESP32-2432S028` boards with **LVGL 8.4** and **PlatformIO**.

---

## What It Does

- **Material 3 UI**: Switch between **Dark**, **Light**, and *AMOLED Black* themes live.
- **App Store**: <mark>Install and run ESP32 `.bin` apps</mark> from flash or SD.
- **Modular Apps**: Drop one `src/apps/app_*.cpp` file to add a new app.
- **Wi-Fi & Browser**: Connect to 2.4 GHz Wi-Fi and read web pages.
- **Android Companion**: Mirror the screen and reply to **WhatsApp** over local Wi-Fi.
- **BLE Trackpad & Remote**: Control phones or Fire TV via *Bluetooth HID*.
- **On-Screen Keyboard**: Slide across keys with a live floating key preview.
- **Persistent Storage**: Save Wi-Fi, notes, timezone, and chats to ESP32 NVS.

## Supported Hardware

- **Dual-USB CYD (`cyd2usb`)**: *Default target.* Uses `ST7789` display and `GPIO 27` backlight.
- **Classic CYD (`cyd`)**: Single-USB board. Uses `ILI9341` display and `GPIO 21` backlight.
- **Touch**: `XPT2046` resistive touch with light-tap tuning and noise filtering.
- **MicroSD**: Optional VSPI card slot for apps, files, and backups.

## Quick Start

<u>Install PlatformIO</u> before building the firmware.

```bash
# Build both boards
pio run -e cyd2usb -e cyd

# Verify partition layout
python3 tools/verify_partitions.py

# Flash Dual-USB CYD (default)
pio run -e cyd2usb -t upload

# Flash Classic CYD
pio run -e cyd -t upload
```

Hold **BOOT** during reset to recalibrate touch.

## Android Companion

The companion app lives in `android-companion/`.

1. Open **Android Link** on your CYD.
2. Scan the QR code to install `pixel-cyd-companion.apk`.
3. Grant **Notification Access** on your phone.
4. Join the same *2.4 GHz Wi-Fi* network.
5. Scan the pairing QR code to connect.

## Add a New App

Create one file at `src/apps/app_mytool.cpp`:

```cpp
#include "app_registry.h"
#include "../components/m3_widgets.h"

static void buildMyTool(lv_obj_t* body) {
    lv_obj_t* lbl = lv_label_create(body);
    lv_label_set_text(lbl, "Hello CYD!");
}

REGISTER_APP(mytool, "mytool", "My Tool", LV_SYMBOL_OK,
             0x8AB4F8, 0x1D3557, buildMyTool, nullptr, 170);
```

Rebuild and flash. No core UI edits are needed.

---

## LLM Context

<u>Machine-readable summary for AI coding assistants.</u>

### Stack & Targets
- **MCU**: ESP32-WROOM-32, 4 MB flash, 320 KB RAM, no PSRAM.
- **Framework**: PlatformIO Arduino, `LVGL 8.4` (`LV_MEM_CUSTOM 1`), `TFT_eSPI`, `NimBLE-Arduino`.
- **Environments**: `cyd2usb` (`ST7789`, default), `cyd` (`ILI9341`).
- **Companion**: Kotlin Jetpack Compose Android app in `android-companion/`.

### Flash Partition Map (`partitions_launcher_4mb.csv`)
- `nvs` (`0x009000`, 20 KB): Settings, Wi-Fi, notes, WhatsApp, App Store registry.
- `otadata` (`0x00E000`, 8 KB): Boot slot selector (`factory` vs installed app).
- `factory` (`0x010000`, 2.00 MB): Main cydOS firmware (~97.7% full).
- **Dynamic Pool** (`0x210000..0x400000`, 1.94 MB): Runtime-managed installed apps and data.

### Key Directories
- `src/apps/`: 21 modular apps (`app_store`, `app_whatsapp`, `app_trackpad`, `app_screen_mirror`, etc.).
- `src/launcher/`: Dynamic partitioner, LauncherHub OTA, SD flasher, backup, WebUI.
- `src/drivers/`: Display, touch, Wi-Fi, NTP, HTTP, BLE, HID, WebSocket, mirror.
- `src/components/`: Shared Material 3 widgets, `m3_modal`, and keyboard.
- `src/ui/` & `src/theme/`: Home screen, status bar, nav bar, themes.
- `tools/`: Host C++ unit tests and `verify_partitions.py`.

### Critical Constraints
- **DRAM Budget**: Heap is tight during HTTPS TLS (~77 KB free).
- **TLS Safety**: Free Explore cards before HTTPS calls. Release Classic BT DRAM.
- **Stack Size**: `loopTask` uses a 14 KB stack for TLS calls.
- **Touch Pins**: `CLK=25`, `MOSI=32`, `MISO=39`, `CS=33`, `IRQ=36`.
- **SD Pins (VSPI)**: `SCK=18`, `MISO=19`, `MOSI=23`, `CS=5`.
- **Companion Protocol**: RFC 6455 WebSocket on `:8765`, pairing endpoint on `:8766`.

### Verification Commands
- **Build**: `pio run -e cyd2usb -e cyd`
- **Partitions**: `python3 tools/verify_partitions.py`
- **Companion APK**: Built via `.github/workflows/build-companion-apk.yml`.
