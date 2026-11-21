# Pixel CYD Android Companion (WhatsApp Bridge)

A lightweight, local-network-only Kotlin Android companion app that forwards **WhatsApp** notifications to the **ESP32 Pixel CYD** (`pixel-cyd`) over WebSocket and supports sending quick replies via Android's official `RemoteInput` API.

- **No cloud, no account, no database server.**
- **Local Wi-Fi only** with a 6-digit pairing token.
- **Privacy-first**: message history is stored locally only (up to 30 recent chats in Android `SharedPreferences`, up to 8 persisted chats + recent bubbles in CYD ESP32 NVS flash) and is never sent to any cloud or logged to Logcat.

---

## 1. Architecture Overview

| Component | Location | Responsibility |
| --- | --- | --- |
| **Android Companion App** | `android-companion/` | Runs a Foreground Service + RFC 6455 WebSocket server (`:8765`), listens to WhatsApp notifications via `NotificationListenerService` (extracting `MessagingStyle` conversation bubbles and `24×24` RGB565 contact avatars), persists local history, executes `RemoteInput` replies & dismissals, and advertises `_pixelcyd._tcp.` via NSD. |
| **CYD Companion Driver** | `pixel-cyd/src/drivers/companion_mgr.{h,cpp}` | Background FreeRTOS task maintaining a non-blocking RFC 6455 WebSocket connection with reconnect backoff, heartbeat, NVS config & chat persistence (`wachats`), per-chat bubble history, and on-demand avatar decoding. |
| **CYD WhatsApp App** | `pixel-cyd/src/apps/app_whatsapp.cpp` | Modular `REGISTER_APP_THEMED_IMG` LVGL app mirroring WhatsApp Android: Chat List with circular profile avatars (or green WhatsApp icon fallback) & unread badges, Conversation View with incoming/outgoing bubbles & rounded composer, Setup screen, and black `?` help button in the title bar. |

---

## 2. Build & Install

### Android Companion App
1. Open `android-companion/` in **Android Studio** (Ladybug 2024.2+ or newer, JDK 17, Android SDK 35).
2. Let Gradle sync, then click **Run** (or `./gradlew assembleDebug` once Gradle wrapper is initialized in Android Studio) to install on your Android phone (Android 8.0 / API 26+).

### ESP32 CYD Firmware
```bash
cd pixel-cyd

# Build both board targets
pio run -e cyd2usb -e cyd

# Flash Dual-USB CYD (default)
pio run -e cyd2usb -t upload

# Flash Classic Single-USB CYD
pio run -e cyd -t upload
```

---

## 3. Android Permissions & One-Tap QR Pairing

1. Open **Android Link** on the CYD and scan the Step 1 QR code with your phone to download & install `pixel-cyd-companion.apk` (if Play Protect prompts, tap **More details → Install anyway**).
2. Open **Pixel CYD Companion** on your Android phone and allow **Post Notifications** (Android 13+).
3. Tap **Grant Notification Access** and follow the built-in 2-step guide:
   - Tap **Open Notification Settings** → **Pixel CYD WhatsApp Bridge** → tap the **Allow notification access** switch (even if grayed out, to trigger Android's Restricted Setting check).
   - If blocked by Android 13+ *Restricted setting*, tap **Next (If Blocked)** → **Open App Info** → **⋮ → Allow restricted settings** (or bottom of App Info on Android 15), then return and turn on **Pixel CYD WhatsApp Bridge**.
4. Ensure both your Android phone and the ESP32 CYD are connected to the **same 2.4 GHz Wi-Fi network**.
5. On the CYD, tap **App Installed →** to display the Pairing QR, then tap **Scan CYD QR Code** inside **Pixel CYD Companion** on your phone.
   - *(You can also enter Phone IP, Port `8765`, and the 6-digit Pairing Token manually in CYD **WhatsApp → gear icon**.)*
6. Tap **Send Test Msg** in the Android Companion app (or receive a real WhatsApp notification while WhatsApp is backgrounded) to verify instant delivery to the CYD!

---

## 4. Communication Protocol (v1)

Transport: **RFC 6455 WebSocket** at `ws://<phone-ip>:8765/ws` (UTF-8 JSON text frames, max 8 KB inbound on phone / 4 KB on CYD).

All messages include `"v": 1` and `"type"`. The schema is intentionally extensible for future features (e.g., `camera_status`).

### Handshake & Authentication
CYD → Android:
```json
{ "v": 1, "type": "hello", "device": "Pixel-CYD", "token": "482910" }
```
Android → CYD:
```json
{ "v": 1, "type": "hello_ack", "ok": true, "forwarding_enabled": true }
```

### Live WhatsApp Notification
Android → CYD:
```json
{
  "v": 1,
  "type": "notification",
  "id": "a1b2c3d4e5f60718293a",
  "sender": "Alex",
  "conversation": "Alex",
  "text": "Are we still on for 7pm?",
  "timestamp": 1763720400000,
  "time": "19:20",
  "can_reply": true
}
```

### History Sync
CYD → Android:
```json
{ "v": 1, "type": "history_request", "limit": 12 }
```
Android → CYD:
```json
{
  "v": 1,
  "type": "history_response",
  "items": [
    {
      "id": "a1b2c3d4e5f60718293a",
      "sender": "Alex",
      "conversation": "Alex",
      "text": "Are we still on for 7pm?",
      "timestamp": 1763720400000,
      "time": "19:20",
      "can_reply": true
    }
  ]
}
```

### Reply via `RemoteInput`
CYD → Android:
```json
{ "v": 1, "type": "reply_request", "req_id": 1, "id": "a1b2c3d4e5f60718293a", "text": "Yes, see you there!" }
```
Android → CYD (`status`: `"sent"`, `"unsupported"`, or `"failed"`):
```json
{ "v": 1, "type": "reply_result", "req_id": 1, "status": "sent" }
```

### Dismiss Notification
CYD → Android:
```json
{ "v": 1, "type": "dismiss_request", "id": "a1b2c3d4e5f60718293a" }
```
Android → CYD:
```json
{ "v": 1, "type": "dismiss_result", "id": "a1b2c3d4e5f60718293a", "ok": true }
```

### Status & Heartbeat
- Android → CYD: `{ "v": 1, "type": "status", "forwarding_enabled": true, "listener_connected": true }`
- CYD ↔ Android: WebSocket Ping/Pong frames every 20s (plus optional JSON `ping`/`pong`).

### Live Screen Mirror & Programmatic Remote Control
- **Toggle Stream** (Android → CYD): `{ "v": 1, "type": "mirror_control", "enable": true }`
- **Screen Bands** (CYD → Android): Binary WebSocket frames (`opcode 0x02`) starting with `CY\x01` + band metadata + PackBits 16-bit Big-Endian RGB565 RLE payload (`120×160` in 10 dirty bands).
- **UI Tree Inspection** (Android → CYD): `{ "v": 1, "type": "ui_tree_request" }` → CYD responds with `{ "v": 1, "type": "ui_tree", "w": 240, "h": 320, "active_app": "...", "elements": [{ "id", "type", "label", "x", "y", "w", "h", "cx", "cy" }] }`.
- **Touch Injection** (Android → CYD): `{ "v": 1, "type": "remote_input", "action": "tap"|"down"|"move"|"up"|"swipe", "x": 120, "y": 160 }`.
- **Programmatic Commands** (Android → CYD): `{ "v": 1, "type": "remote_cmd", "cmd": "home"|"back"|"quick_settings"|"launch_app"|"tap_label"|"brightness"|"refresh_ui", ... }`.

---

## 5. Known Limitations

- **WhatsApp replies require an active replyable notification**: Android only allows replying via `Notification.Action` + `RemoteInput` while the WhatsApp notification is still active in the Android shade. If the notification was already dismissed on the phone, cleared by WhatsApp, or lacks a `RemoteInput` action (e.g., missed-call or system alerts), the CYD displays `Unsupported` / `Reply unavailable` instead of pretending the message was sent.
- **Cannot initiate brand-new conversations**: Only replies to active WhatsApp notifications are supported through official Android APIs.
- **Local network only**: Both devices must be on the same LAN/Wi-Fi subnet without AP client isolation.
- **Emoji / non-Latin glyphs**: LVGL's built-in Montserrat fonts render ASCII/Latin characters; emoji in WhatsApp previews may appear as placeholder boxes on the CYD.

---

## 6. Test Checklist

- [ ] Install Android Companion app and grant Notification Access; verify `Notification Access: Granted` and `Listener Service: Running`.
- [ ] Enter Phone IP, Port `8765`, and Pairing Token on CYD WhatsApp Setup; verify green `Connected` status on both devices.
- [ ] Test invalid pairing token; verify CYD shows `Bad token` and Android logs `Rejected invalid pairing token`.
- [ ] Receive a real WhatsApp message on the phone; verify CYD shows toast + unread count in status bar + message in Inbox.
- [ ] Open the message on CYD, type a reply with the on-screen keyboard, tap **Send**; verify WhatsApp sends the reply and CYD shows `Reply sent` before clearing the input.
- [ ] Dismiss the WhatsApp notification on the phone, then attempt a reply from CYD; verify CYD reports `Unsupported` and keeps the typed text intact.
- [ ] Tap **Dismiss** on CYD for an active notification; verify it disappears from the Android notification shade and CYD list.
- [ ] Toggle Wi-Fi off/on or stop/start the Android server; verify CYD preserves its local cache while offline and reconnects automatically with backoff.
