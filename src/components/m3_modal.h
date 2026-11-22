#pragma once

#include <lvgl.h>
#include <stdint.h>

struct M3ModalBtn {
    const char* label;
    uint32_t bgHex;      // 0 = default surface highest
    uint32_t fgHex;      // 0 = default text primary
    void (*onClick)();   // optional callback
    bool closeOnClick;   // close modal when tapped
};

struct M3ModalStep {
    const char* title;
    const char* body;
};

// Simple centered modal with optional action buttons (defaults to an "OK" button if none provided).
void m3ModalShow(const char* title,
                 const char* body,
                 const M3ModalBtn* buttons = nullptr,
                 uint8_t buttonCount = 0,
                 uint32_t accentHex = 0);

// Multi-step centered info / how-to modal with Prev / Next / Done navigation and top-right X close.
void m3ModalShowSteps(const M3ModalStep* steps,
                      uint8_t stepCount,
                      uint32_t accentHex = 0);

// Real-time progress modal with an animated lv_bar and plain-English step narration.
// Starts in indeterminate sweeping mode until m3ModalUpdateProgress() receives pct in 0..100.
// When cancellable == false, the top-right X button and nav-bar dismissal are locked until
// m3ModalFinishProgress() or m3ModalClose() is called.
void m3ModalShowProgress(const char* title,
                         const char* stepText = nullptr,
                         uint32_t accentHex = 0,
                         bool cancellable = false,
                         void (*onCancel)() = nullptr);

// Update an open progress modal in real time.
// pct: 0..100 for determinate animated bar, or < 0 for indeterminate sweeping animation.
// stepText: plain-English narration of the current step (nullptr keeps current text).
// detailText: optional secondary line below the bar, e.g. "420 KB / 1.1 MB" (nullptr keeps current text).
// pumpUi: if true, throttled lv_timer_handler() keeps the animation smooth during blocking work.
void m3ModalUpdateProgress(int8_t pct,
                           const char* stepText = nullptr,
                           const char* detailText = nullptr,
                           bool pumpUi = true);

// Lock or unlock dismissal/cancellation on an active progress modal without
// closing and recreating it (e.g. lock once flash partition commit begins).
void m3ModalSetProgressLocked(bool locked);

// Transition an open progress modal to a completed (success) or failed (!success) state,
// unlocking dismissal and showing action buttons (defaults to "Done" or "Close").
// If no progress modal is open, falls back to m3ModalShow().
void m3ModalFinishProgress(bool success,
                           const char* headline,
                           const char* detail = nullptr,
                           const M3ModalBtn* buttons = nullptr,
                           uint8_t buttonCount = 0);

void m3ModalClose();
bool m3ModalIsOpen();
bool m3ModalIsProgressOpen();
bool m3ModalIsLocked();
