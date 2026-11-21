# Pixel 8 CYD UI (LVGL)

A Pixel 8-inspired Material 3 (Material You) Android UI firmware for the **ESP32 Cheap Yellow Display (CYD / ESP32-2432S028)** built with **LVGL 8.3** and **TFT_eSPI**.

## Features
- **240×320 Portrait** Material 3 interface tuned for ESP32 DRAM (~266 KB free heap at boot)
- **Status Bar**: Time, Bluetooth, Wi-Fi, cellular signal bars, airplane mode, and battery indicator
- **Quick Settings Shade**: Swipe down from the status bar for Wi-Fi, Bluetooth, Airplane Mode, Torch (onboard RGB LED), and live PWM display brightness slider
- **Home Screen**: At-a-Glance date & weather widget, 2-page app grid, Google Search bar, and app launcher
- **13 Interactive Apps**: Settings, Music, Files, Gallery, Browser, Terminal, Wi-Fi, Bluetooth, Sys Info, Calculator, Notes, About, and Clock/Calendar

## Hardware
- **Target**: ESP32-2432S028 (`cyd2usb` ST7789 + GPIO 27 backlight, and `cyd` ILI9341 + GPIO 21 backlight)
- **Touch**: XPT2046 resistive touch on dedicated pins (`CLK=25, MOSI=32, MISO=39, CS=33, IRQ=36`)

## Build & Flash
```bash
pio run -e cyd2usb -t upload
```
Hold the **BOOT** button during reset (or use **Settings → Recalibrate Touch**) to run interactive 2-point touch calibration.
