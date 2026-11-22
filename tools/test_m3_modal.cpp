// Host-side unit test for src/components/m3_modal.cpp progress modal API.
// Compiles m3_modal.cpp directly against a lightweight LVGL 8.3 mock.
// Run via: c++ -std=c++17 tools/test_m3_modal.cpp -o /tmp/test_m3_modal && /tmp/test_m3_modal

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal LVGL 8.3 & project mock
// ---------------------------------------------------------------------------
using lv_coord_t = int16_t;
using lv_opa_t   = uint8_t;
struct lv_color_t { uint32_t hex; };
struct lv_font_t { int size; };
struct lv_event_t { void* user_data; };
struct lv_obj_t;

using lv_event_cb_t = void (*)(lv_event_t*);
using lv_anim_exec_xcb_t = void (*)(void*, int32_t);
using lv_anim_path_cb_t  = int32_t (*)(const void*);

static lv_font_t lv_font_montserrat_10{10};
static lv_font_t lv_font_montserrat_12{12};
static lv_font_t lv_font_montserrat_14{14};

#define LV_PCT(x) (10000 + (x))
#define LV_SIZE_CONTENT (-1)
#define LV_OPA_TRANSP 0
#define LV_OPA_30     76
#define LV_OPA_40     102
#define LV_OPA_50     127
#define LV_OPA_60     153
#define LV_OPA_80     204
#define LV_OPA_COVER  255

#define LV_PART_MAIN      0
#define LV_PART_INDICATOR 1

#define LV_STATE_DISABLED 0x0080

#define LV_OBJ_FLAG_HIDDEN     0x01
#define LV_OBJ_FLAG_CLICKABLE  0x02
#define LV_OBJ_FLAG_SCROLLABLE 0x04

#define LV_FLEX_FLOW_ROW    0
#define LV_FLEX_FLOW_COLUMN 1
#define LV_FLEX_ALIGN_START         0
#define LV_FLEX_ALIGN_CENTER        1
#define LV_FLEX_ALIGN_SPACE_BETWEEN 2

#define LV_DIR_VER 1
#define LV_SCROLLBAR_MODE_AUTO 1
#define LV_LABEL_LONG_WRAP 0
#define LV_LABEL_LONG_DOT  1

#define LV_BAR_MODE_NORMAL 0
#define LV_BAR_MODE_RANGE  1
#define LV_ANIM_OFF 0
#define LV_ANIM_ON  1
#define LV_ANIM_REPEAT_INFINITE 0xFFFF

#define LV_EVENT_CLICKED 1
#define LV_SYMBOL_CLOSE "X"
#define LV_SYMBOL_OK    "OK"

struct lv_obj_t {
    enum Kind { OBJ, BTN, LABEL, BAR } kind = OBJ;
    lv_obj_t* parent = nullptr;
    std::vector<lv_obj_t*> children;
    std::string text;
    uint32_t flags = LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE;
    uint32_t state = 0;
    int32_t bar_mode = LV_BAR_MODE_NORMAL;
    int32_t bar_min = 0;
    int32_t bar_max = 100;
    int32_t bar_start = 0;
    int32_t bar_val = 0;
    bool bar_last_anim = false;
    uint32_t text_color = 0;
    uint32_t indicator_color = 0;
    lv_event_cb_t click_cb = nullptr;
    void* click_ud = nullptr;
};

struct lv_anim_t {
    void* var = nullptr;
    int32_t start = 0;
    int32_t end = 0;
    uint32_t time = 0;
    uint16_t repeat_cnt = 0;
    lv_anim_path_cb_t path_cb = nullptr;
    lv_anim_exec_xcb_t exec_cb = nullptr;
};

static lv_obj_t s_topLayer;
static std::vector<lv_anim_t> s_activeAnims;
static uint32_t s_mockTickMs = 1000;
static int s_timerHandlerCalls = 0;
static int s_pulseCalls = 0;

static lv_obj_t* lv_layer_top() { return &s_topLayer; }
static lv_color_t lv_color_hex(uint32_t c) { return {c}; }
static uint32_t lv_tick_get() { return s_mockTickMs; }
static void lv_timer_handler() { ++s_timerHandlerCalls; }

static lv_obj_t* makeObj(lv_obj_t* parent, lv_obj_t::Kind k) {
    auto* o = new lv_obj_t();
    o->kind = k;
    o->parent = parent;
    if (parent) parent->children.push_back(o);
    return o;
}
static lv_obj_t* lv_obj_create(lv_obj_t* p) { return makeObj(p, lv_obj_t::OBJ); }
static lv_obj_t* lv_btn_create(lv_obj_t* p) { return makeObj(p, lv_obj_t::BTN); }
static lv_obj_t* lv_label_create(lv_obj_t* p) { return makeObj(p, lv_obj_t::LABEL); }
static lv_obj_t* lv_bar_create(lv_obj_t* p) { return makeObj(p, lv_obj_t::BAR); }

static void deleteSubtree(lv_obj_t* o) {
    if (!o) return;
    for (auto* c : o->children) deleteSubtree(c);
    o->children.clear();
    delete o;
}
static void lv_obj_clean(lv_obj_t* o) {
    for (auto* c : o->children) deleteSubtree(c);
    o->children.clear();
}
static void lv_obj_del(lv_obj_t* o) {
    if (!o) return;
    if (o->parent) {
        auto& v = o->parent->children;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == o) { v.erase(v.begin() + i); break; }
        }
    }
    deleteSubtree(o);
}

static lv_obj_t* lv_obj_get_child(lv_obj_t* o, int idx) {
    if (!o || idx < 0 || (size_t)idx >= o->children.size()) return nullptr;
    return o->children[idx];
}

static void lv_obj_set_size(lv_obj_t*, lv_coord_t, lv_coord_t) {}
static void lv_obj_set_width(lv_obj_t*, lv_coord_t) {}
static void lv_obj_set_pos(lv_obj_t*, lv_coord_t, lv_coord_t) {}
static void lv_obj_center(lv_obj_t*) {}
static void lv_obj_move_foreground(lv_obj_t*) {}
static void lv_obj_set_flex_flow(lv_obj_t*, int) {}
static void lv_obj_set_flex_align(lv_obj_t*, int, int, int) {}
static void lv_obj_set_flex_grow(lv_obj_t*, int) {}
static void lv_obj_set_scroll_dir(lv_obj_t*, int) {}
static void lv_obj_set_scrollbar_mode(lv_obj_t*, int) {}
static void lv_obj_add_flag(lv_obj_t* o, uint32_t f) { o->flags |= f; }
static void lv_obj_clear_flag(lv_obj_t* o, uint32_t f) { o->flags &= ~f; }
static bool lv_obj_has_flag(lv_obj_t* o, uint32_t f) { return (o->flags & f) != 0; }
static void lv_obj_add_state(lv_obj_t* o, uint32_t s) { o->state |= s; }
static void lv_obj_clear_state(lv_obj_t* o, uint32_t s) { o->state &= ~s; }

static void lv_obj_set_style_bg_color(lv_obj_t* o, lv_color_t c, int part) {
    if (part == LV_PART_INDICATOR) o->indicator_color = c.hex;
}
static void lv_obj_set_style_bg_opa(lv_obj_t*, lv_opa_t, int) {}
static void lv_obj_set_style_border_width(lv_obj_t*, lv_coord_t, int) {}
static void lv_obj_set_style_border_color(lv_obj_t*, lv_color_t, int) {}
static void lv_obj_set_style_border_opa(lv_obj_t*, lv_opa_t, int) {}
static void lv_obj_set_style_radius(lv_obj_t*, lv_coord_t, int) {}
static void lv_obj_set_style_shadow_width(lv_obj_t*, lv_coord_t, int) {}
static void lv_obj_set_style_shadow_opa(lv_obj_t*, lv_opa_t, int) {}
static void lv_obj_set_style_shadow_ofs_y(lv_obj_t*, lv_coord_t, int) {}
static void lv_obj_set_style_shadow_color(lv_obj_t*, lv_color_t, int) {}
static void lv_obj_set_style_pad_all(lv_obj_t*, lv_coord_t, int) {}
static void lv_obj_set_style_pad_hor(lv_obj_t*, lv_coord_t, int) {}
static void lv_obj_set_style_pad_ver(lv_obj_t*, lv_coord_t, int) {}
static void lv_obj_set_style_pad_row(lv_obj_t*, lv_coord_t, int) {}
static void lv_obj_set_style_pad_column(lv_obj_t*, lv_coord_t, int) {}
static void lv_obj_set_style_text_font(lv_obj_t*, const lv_font_t*, int) {}
static void lv_obj_set_style_text_color(lv_obj_t* o, lv_color_t c, int) { o->text_color = c.hex; }
static void lv_obj_set_style_text_line_space(lv_obj_t*, lv_coord_t, int) {}
static void lv_obj_set_style_opa(lv_obj_t*, lv_opa_t, int) {}
static void lv_obj_set_style_anim_time(lv_obj_t*, uint32_t, int) {}

static void lv_label_set_text(lv_obj_t* o, const char* t) { o->text = t ? t : ""; }
static const char* lv_label_get_text(lv_obj_t* o) { return o->text.c_str(); }
static void lv_label_set_long_mode(lv_obj_t*, int) {}

static void lv_bar_set_range(lv_obj_t* o, int32_t mn, int32_t mx) { o->bar_min = mn; o->bar_max = mx; }
static void lv_bar_set_mode(lv_obj_t* o, int32_t m) { o->bar_mode = m; }
static void lv_bar_set_start_value(lv_obj_t* o, int32_t v, int) { o->bar_start = v; }
static void lv_bar_set_value(lv_obj_t* o, int32_t v, int anim) { o->bar_val = v; o->bar_last_anim = (anim == LV_ANIM_ON); }

static void lv_obj_add_event_cb(lv_obj_t* o, lv_event_cb_t cb, int, void* ud) {
    o->click_cb = cb;
    o->click_ud = ud;
}
static void* lv_event_get_user_data(lv_event_t* e) { return e->user_data; }

static int32_t lv_anim_path_ease_in_out(const void*) { return 0; }
static void lv_anim_init(lv_anim_t* a) { *a = lv_anim_t{}; }
static void lv_anim_set_var(lv_anim_t* a, void* v) { a->var = v; }
static void lv_anim_set_values(lv_anim_t* a, int32_t s, int32_t e) { a->start = s; a->end = e; }
static void lv_anim_set_time(lv_anim_t* a, uint32_t t) { a->time = t; }
static void lv_anim_set_repeat_count(lv_anim_t* a, uint16_t r) { a->repeat_cnt = r; }
static void lv_anim_set_path_cb(lv_anim_t* a, lv_anim_path_cb_t cb) { a->path_cb = cb; }
static void lv_anim_set_exec_cb(lv_anim_t* a, lv_anim_exec_xcb_t cb) { a->exec_cb = cb; }
static void lv_anim_start(const lv_anim_t* a) { s_activeAnims.push_back(*a); }
static bool lv_anim_del(void* var, lv_anim_exec_xcb_t cb) {
    bool removed = false;
    for (auto it = s_activeAnims.begin(); it != s_activeAnims.end(); ) {
        if (it->var == var && (cb == nullptr || it->exec_cb == cb)) {
            it = s_activeAnims.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }
    return removed;
}

// Mock project dependencies included by m3_modal.cpp
namespace M3Color {
    constexpr uint32_t PRIMARY  = 0x8AB4F8;
    constexpr uint32_t TERTIARY = 0x81C995;
    constexpr uint32_t CORAL    = 0xF28B82;
}
namespace M3Dim {
    constexpr int16_t RADIUS_PILL = 999;
}
static uint32_t m3SurfaceHex()        { return 0x191C24; }
static uint32_t m3SurfaceHighestHex() { return 0x2C313F; }
static uint32_t m3OutlineHex()        { return 0x353B4B; }
static uint32_t m3TextPrimaryHex()    { return 0xE8EAED; }
static uint32_t m3TextSecondaryHex()  { return 0x9AA0A6; }
static uint32_t m3PrimaryHex()        { return M3Color::PRIMARY; }
static void m3PulseFeedback()         { ++s_pulseCalls; }
static uint16_t displayWidth()        { return 240; }
static uint16_t displayHeight()       { return 320; }

static lv_obj_t* m3PillBtn(lv_obj_t* parent, const char* text, uint32_t, uint32_t, lv_coord_t, lv_coord_t) {
    lv_obj_t* b = lv_btn_create(parent);
    lv_obj_t* l = lv_label_create(b);
    lv_label_set_text(l, text ? text : "");
    return b;
}

// Suppress real hardware/LVGL header includes when compiling m3_modal.cpp on host
#define M3_MODAL_HOST_TEST
struct M3ModalBtn {
    const char* label;
    uint32_t bgHex;
    uint32_t fgHex;
    void (*onClick)();
    bool closeOnClick;
};
struct M3ModalStep {
    const char* title;
    const char* body;
};
void m3ModalShow(const char* title, const char* body, const M3ModalBtn* buttons = nullptr, uint8_t buttonCount = 0, uint32_t accentHex = 0);
void m3ModalShowSteps(const M3ModalStep* steps, uint8_t stepCount, uint32_t accentHex = 0);
void m3ModalShowProgress(const char* title, const char* stepText = nullptr, uint32_t accentHex = 0, bool cancellable = false, void (*onCancel)() = nullptr);
void m3ModalUpdateProgress(int8_t pct, const char* stepText = nullptr, const char* detailText = nullptr, bool pumpUi = true);
void m3ModalFinishProgress(bool success, const char* headline, const char* detail = nullptr, const M3ModalBtn* buttons = nullptr, uint8_t buttonCount = 0);
void m3ModalClose();
bool m3ModalIsOpen();
bool m3ModalIsProgressOpen();
bool m3ModalIsLocked();
#include "../src/components/m3_modal.cpp"

// Helper to simulate a click
static void clickObj(lv_obj_t* o) {
    assert(o && o->click_cb);
    lv_event_t ev{o->click_ud};
    o->click_cb(&ev);
}

static int s_cancelFired = 0;
static void onCancelCb() { ++s_cancelFired; }

static int s_launchFired = 0;
static void onLaunchCb() { ++s_launchFired; }

int main() {
    // 1. Locked progress modal opens in indeterminate mode
    s_timerHandlerCalls = 0;
    m3ModalShowProgress("Installing App", "Connecting to LauncherHub...", 0x8AB4F8, false);
    assert(m3ModalIsOpen());
    assert(m3ModalIsProgressOpen());
    assert(m3ModalIsLocked());
    assert(s_closeBtn && lv_obj_has_flag(s_closeBtn, LV_OBJ_FLAG_HIDDEN));
    assert(s_progBar && s_progBar->bar_mode == LV_BAR_MODE_RANGE);
    assert(s_activeAnims.size() == 1);
    assert(s_bodyLbl->text == "Connecting to LauncherHub...");
    assert(s_progPctLbl->text == "...");
    assert(s_timerHandlerCalls == 1);

    // Exercise indeterminate animation callback bounds
    s_activeAnims[0].exec_cb(s_progBar, -30);
    assert(s_progBar->bar_start == 0 && s_progBar->bar_val == 0);
    s_activeAnims[0].exec_cb(s_progBar, 35);
    assert(s_progBar->bar_start == 35 && s_progBar->bar_val == 65);
    s_activeAnims[0].exec_cb(s_progBar, 100);
    assert(s_progBar->bar_start == 100 && s_progBar->bar_val == 100);

    // Clicking hidden close button while locked must be a no-op
    clickObj(s_closeBtn);
    assert(m3ModalIsOpen());

    // 2. Transition to determinate progress (0..100), animated, clamped, throttled pumping
    m3ModalUpdateProgress(25, "Downloading firmware...", "250 KB / 1.0 MB");
    assert(s_activeAnims.empty());
    assert(s_progBar->bar_mode == LV_BAR_MODE_NORMAL);
    assert(s_progBar->bar_val == 25 && s_progBar->bar_last_anim);
    assert(s_progPctLbl->text == "25%");
    assert(s_bodyLbl->text == "Downloading firmware...");
    assert(s_progDetailLbl->text == "250 KB / 1.0 MB");
    assert(s_timerHandlerCalls == 2); // stepText changed -> immediate pump

    // Rapid update within 5ms with same stepText should NOT pump
    s_mockTickMs += 5;
    m3ModalUpdateProgress(26, "Downloading firmware...", "260 KB / 1.0 MB");
    assert(s_progBar->bar_val == 26);
    assert(s_timerHandlerCalls == 2);

    // After 25ms, update SHOULD pump
    s_mockTickMs += 25;
    m3ModalUpdateProgress(50, nullptr, "500 KB / 1.0 MB");
    assert(s_progBar->bar_val == 50);
    assert(s_bodyLbl->text == "Downloading firmware...");
    assert(s_timerHandlerCalls == 3);

    // Clamping > 100 -> 100 and immediate pump on 100%
    s_mockTickMs += 2;
    m3ModalUpdateProgress(120, nullptr, "1.0 MB / 1.0 MB");
    assert(s_progBar->bar_val == 100);
    assert(s_progPctLbl->text == "100%");
    assert(s_timerHandlerCalls == 4);

    // Transition back to indeterminate (-1) e.g. while verifying / partitioning
    m3ModalUpdateProgress(-1, "Setting up app data...", "");
    assert(s_progBar->bar_mode == LV_BAR_MODE_RANGE);
    assert(s_activeAnims.size() == 1);
    assert(s_progPctLbl->text == "...");

    // 3. Finish with success + custom action buttons
    M3ModalBtn doneBtns[2] = {
        {"Later", 0, 0, nullptr, true},
        {"Launch", M3Color::TERTIARY, 0x0F1117, onLaunchCb, true},
    };
    m3ModalFinishProgress(true, "App Installed!", "Added to Home Screen.", doneBtns, 2);
    assert(!m3ModalIsLocked());
    assert(!lv_obj_has_flag(s_closeBtn, LV_OBJ_FLAG_HIDDEN));
    assert(s_activeAnims.empty());
    assert(s_progBar->bar_val == 100);
    assert(s_progBar->indicator_color == M3Color::TERTIARY);
    assert(s_titleLbl->text == "App Installed!");
    assert(s_bodyLbl->text == "Added to Home Screen.");
    assert(s_footer->children.size() == 2);

    clickObj(s_footer->children[1]);
    assert(s_launchFired == 1);
    assert(!m3ModalIsOpen());
    assert(!m3ModalIsProgressOpen());

    // 4. Cancellable progress modal + cancel callback
    s_cancelFired = 0;
    m3ModalShowProgress("Downloading...", "Fetching manifest...", 0, true, onCancelCb);
    assert(m3ModalIsOpen() && m3ModalIsProgressOpen() && !m3ModalIsLocked());
    assert(!lv_obj_has_flag(s_closeBtn, LV_OBJ_FLAG_HIDDEN));
    assert(s_cancelBtn != nullptr);
    clickObj(s_cancelBtn);
    assert(s_cancelFired == 1);
    assert(s_cancelBtn->state & LV_STATE_DISABLED);
    assert(s_cancelBtn->children[0]->text == "Cancelling...");
    // Second click must not re-fire callback
    clickObj(s_cancelBtn);
    assert(s_cancelFired == 1);
    m3ModalClose();
    assert(!m3ModalIsOpen());
    assert(s_activeAnims.empty());

    // 5. Finish with error while indeterminate resets bar to 0 and shows Close button
    m3ModalShowProgress("Flashing...", "Erasing partition...", 0, false);
    m3ModalFinishProgress(false, "Install Failed", "Not enough free flash space.");
    assert(!m3ModalIsLocked());
    assert(s_progBar->bar_val == 0);
    assert(s_progBar->indicator_color == M3Color::CORAL);
    assert(s_footer->children.size() == 1);
    assert(s_footer->children[0]->children[0]->text == "Close");
    m3ModalClose();

    // 6. Fallback when m3ModalFinishProgress is called with no progress modal open
    m3ModalFinishProgress(false, "Network Error", "Wi-Fi disconnected.");
    assert(m3ModalIsOpen());
    assert(!m3ModalIsProgressOpen());
    assert(s_titleLbl->text == "Network Error");
    assert(s_bodyLbl->text == "Wi-Fi disconnected.");
    m3ModalClose();
    assert(!m3ModalIsOpen());

    printf("[OK] test_m3_modal: all progress modal assertions passed!\n");
    return 0;
}
