# Pixel 8 CYD UI (LVGL) — v0.2

A Pixel 8-inspired Material 3 (Material You) Android UI firmware for the **ESP32 Cheap Yellow Display (CYD / ESP32-2432S028)** built with **LVGL 8.3/8.4** and **TFT_eSPI**.

## Features
- **240×320 Portrait** Material 3 interface using ESP32 dynamic heap (`LV_MEM_CUSTOM 1`, ~283 KB free heap at boot)
- **Dark & Light Mode**: Dedicated **Theme** app icon (right after Settings) and Menu shortcut to switch the entire UI—including status bar, navigation bar, quick settings, apps, and keyboard—between Dark and Light themes
- **Status Bar**: Time, Bluetooth, Wi-Fi, 4-bar cellular signal, airplane badge, and battery indicator
- **Persistent Android 3-Button Bottom Bar**: Always-visible `54px` bar with evenly spaced **Back**, **Home**, and **Menu** (hamburger) buttons + quick action menu
- **Quick Settings Shade**: Swipe down or tap the status bar for Wi-Fi, Bluetooth, Airplane Mode, Torch (onboard RGB LED), and live PWM display brightness slider
- **Custom On-Screen Keyboard**: 4-row QWERTY / `123` symbols keyboard that automatically appears when tapping text inputs (Browser URL bar, Notes) and adapts to Dark/Light mode
- **Home Screen**: At-a-Glance date & weather widget (opens Clock/Calendar), swipeable 2-page 4×2 app grid with page indicator dots, and Google Search pill
- **14 Interactive Apps**: Settings, Theme (Dark/Light toggle), Music, Files, Gallery, Browser, Terminal, Wi-Fi, Bluetooth, Sys Info, Calculator, Notes, About, and Clock/Calendar

## Hardware
- **Target**: ESP32-2432S028 (`cyd2usb` ST7789 + GPIO 27 backlight, and `cyd` ILI9341 + GPIO 21 backlight)
- **Touch**: XPT2046 resistive touch on dedicated pins (`CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36`) with portrait X-mirror correction, tuned `TOUCH_Z_MIN=150`, and `scroll_limit=20`

## Build & Flash
```bash
pio run -e cyd2usb -t upload
```
Hold the **BOOT** button during reset (or use **Settings → Recalibrate Touch**) to run interactive 2-point touch calibration.

## Changelog
- **v0.2**: Persistent 3-button Android bottom bar (Back/Home/Menu), Dark/Light Theme app icon, custom Light/Dark on-screen keyboard, ST7789 touch X-mirror fix & light-touch tuning, and `LV_MEM_CUSTOM 1` dynamic heap allocation.
- **v0.1**: Initial Pixel 8 CYD LVGL UI release.
