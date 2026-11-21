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

void m3ModalClose();
bool m3ModalIsOpen();
