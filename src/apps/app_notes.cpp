#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"
#include "../components/m3_keyboard.h"
#include "../drivers/notes_store.h"
#include <stdio.h>
#include <string.h>

enum class NotesView : uint8_t {
    LIST = 0,
    EDITOR
};

static lv_obj_t* s_body     = nullptr;
static lv_obj_t* s_titleTa  = nullptr;
static lv_obj_t* s_bodyTa   = nullptr;
static lv_obj_t* s_backBtn  = nullptr;
static lv_obj_t* s_saveBtn  = nullptr;
static lv_obj_t* s_delBtn   = nullptr;

static NotesView s_view       = NotesView::LIST;
static int8_t    s_editingIdx = -1; // -1 = new unsaved note

static void renderListView();
static void renderEditorView(const char* initTitle, const char* initBody);

static uint32_t amberAccentHex() {
    return g_sys.lightMode ? 0xB26A00 : M3Color::AMBER;
}

static uint32_t amberBtnFgHex() {
    return g_sys.lightMode ? 0xFFFFFF : 0x2B2100;
}

static uint32_t dangerBgHex() {
    return g_sys.lightMode ? 0xD32F2F : 0x5C2323;
}

static uint32_t dangerFgHex() {
    return g_sys.lightMode ? 0xFFFFFF : M3Color::CORAL;
}

static void clearEditorPtrs() {
    s_titleTa = nullptr;
    s_bodyTa  = nullptr;
    s_backBtn = nullptr;
    s_saveBtn = nullptr;
    s_delBtn  = nullptr;
}

static void notesDeleteCb(lv_event_t* e) {
    (void)e;
    s_body = nullptr;
    clearEditorPtrs();
}

static void makePreviewLine(const char* body, char* out, size_t cap) {
    if (!out || cap == 0) return;
    out[0] = '\0';
    if (!body) return;

    size_t j = 0;
    bool prevSpace = true;
    for (size_t i = 0; body[i] && j + 1 < cap; ++i) {
        char c = body[i];
        if (c == '\r' || c == '\n' || c == '\t' || c == ' ') {
            if (!prevSpace) {
                out[j++] = ' ';
                prevSpace = true;
            }
        } else {
            out[j++] = c;
            prevSpace = false;
        }
    }
    if (j > 0 && out[j - 1] == ' ') --j;
    out[j] = '\0';
    if (j == 0) {
        strncpy(out, "(empty)", cap - 1);
        out[cap - 1] = '\0';
    }
}

static void saveCurrentNote() {
    if (!s_titleTa || !s_bodyTa) return;
    const char* t = lv_textarea_get_text(s_titleTa);
    const char* b = lv_textarea_get_text(s_bodyTa);

    int8_t saved = notesStoreSave(s_editingIdx, t, b);
    if (saved < 0) {
        m3ShowToast("Notes full (max 8)", 1400);
        return;
    }

    s_editingIdx = saved;
    const NoteItem* item = notesStoreGet((uint8_t)saved);
    if (item && s_titleTa) {
        lv_textarea_set_text(s_titleTa, item->title);
    }
    m3ShowToast("Saved to NVS", 1100);
}

static void onTitleEnter(const char* txt) {
    (void)txt;
    saveCurrentNote();
}

static void onBodyEnter(const char* txt) {
    (void)txt;
    saveCurrentNote();
}

static void backToListCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    s_view = NotesView::LIST;
    renderListView();
}

static void saveBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();
    saveCurrentNote();
}

static void delBtnCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (m3KeyboardIsOpen()) m3KeyboardHide();

    if (s_editingIdx >= 0) {
        if (notesStoreDelete((uint8_t)s_editingIdx)) {
            m3ShowToast("Note deleted", 1100);
        }
    } else {
        m3ShowToast("Draft discarded", 1000);
    }
    s_editingIdx = -1;
    s_view = NotesView::LIST;
    renderListView();
}

static void newNoteCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    if (notesStoreCount() >= NOTES_MAX_COUNT) {
        m3ShowToast("Notes full (max 8)", 1400);
        return;
    }
    s_editingIdx = -1;
    s_view = NotesView::EDITOR;
    renderEditorView("", "");
}

static void openNoteCb(lv_event_t* e) {
    m3PulseFeedback();
    int8_t idx = (int8_t)(intptr_t)lv_event_get_user_data(e);
    const NoteItem* item = notesStoreGet((uint8_t)idx);
    if (!item) return;

    s_editingIdx = idx;
    s_view = NotesView::EDITOR;
    renderEditorView(item->title, item->body);
}

static void sdExportCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    uint8_t exported = 0;
    NotesSdResult res = notesStoreExportSd(&exported);
    char msg[40];
    switch (res) {
        case NOTES_SD_OK:
            snprintf(msg, sizeof(msg), "Exported %u to SD", (unsigned)exported);
            m3ShowToast(msg, 1500);
            break;
        case NOTES_SD_NO_CARD:
            m3ShowToast("No microSD card", 1500);
            break;
        case NOTES_SD_EMPTY:
            m3ShowToast("No notes to export", 1400);
            break;
        default:
            m3ShowToast("SD write error", 1500);
            break;
    }
}

static void sdImportCb(lv_event_t* e) {
    (void)e;
    m3PulseFeedback();
    uint8_t imported = 0;
    NotesSdResult res = notesStoreImportSd(&imported);
    char msg[40];
    switch (res) {
        case NOTES_SD_OK:
            snprintf(msg, sizeof(msg), "Imported %u from SD", (unsigned)imported);
            m3ShowToast(msg, 1500);
            renderListView();
            break;
        case NOTES_SD_NO_CARD:
            m3ShowToast("No microSD card", 1500);
            break;
        case NOTES_SD_NO_FILE:
            m3ShowToast("No backup on SD", 1500);
            break;
        case NOTES_SD_EMPTY:
            m3ShowToast("SD backup empty/full", 1500);
            break;
        default:
            m3ShowToast("SD read error", 1500);
            break;
    }
}

static void styleTextarea(lv_obj_t* ta) {
    if (!ta) return;
    lv_obj_set_style_bg_color(ta, lv_color_hex(m3SurfaceHighHex()), 0);
    lv_obj_set_style_bg_opa(ta, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(ta, lv_color_hex(m3TextPrimaryHex()), 0);
    lv_obj_set_style_text_font(ta, &lv_font_montserrat_12, 0);
    lv_obj_set_style_border_color(ta, lv_color_hex(amberAccentHex()), 0);
    lv_obj_set_style_border_width(ta, 1, 0);
    lv_obj_set_style_border_opa(ta, LV_OPA_50, 0);
    lv_obj_set_style_radius(ta, 12, 0);
    lv_obj_set_style_pad_hor(ta, 8, 0);
    lv_obj_set_style_pad_ver(ta, 6, 0);
}

static void renderListView() {
    if (!s_body) return;
    clearEditorPtrs();
    lv_obj_clean(s_body);

    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_body, 6, 0);

    // Top action row: + New | SD Exp | SD Imp
    lv_obj_t* topRow = lv_obj_create(s_body);
    lv_obj_set_size(topRow, LV_PCT(100), 30);
    lv_obj_set_style_bg_opa(topRow, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(topRow, 0, 0);
    lv_obj_set_style_pad_all(topRow, 0, 0);
    lv_obj_set_flex_flow(topRow, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(topRow, 4, 0);
    lv_obj_clear_flag(topRow, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t* newBtn = m3PillBtn(topRow, LV_SYMBOL_PLUS " New", amberAccentHex(), amberBtnFgHex(), 0, 30);
    lv_obj_set_flex_grow(newBtn, 1);
    lv_obj_add_event_cb(newBtn, newNoteCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* expBtn = m3PillBtn(topRow, LV_SYMBOL_SD_CARD " Exp", m3SurfaceHighestHex(), m3TextPrimaryHex(), 66, 30);
    lv_obj_add_event_cb(expBtn, sdExportCb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t* impBtn = m3PillBtn(topRow, LV_SYMBOL_DOWNLOAD " Imp", m3SurfaceHighestHex(), m3TextPrimaryHex(), 66, 30);
    lv_obj_add_event_cb(impBtn, sdImportCb, LV_EVENT_CLICKED, nullptr);

    // Count status label
    uint8_t cnt = notesStoreCount();
    char statusTxt[40];
    if (cnt == 0) {
        snprintf(statusTxt, sizeof(statusTxt), "No notes • tap + New");
    } else {
        snprintf(statusTxt, sizeof(statusTxt), "%u / %u saved in NVS",
                 (unsigned)cnt, (unsigned)NOTES_MAX_COUNT);
    }

    lv_obj_t* countLbl = lv_label_create(s_body);
    lv_label_set_text(countLbl, statusTxt);
    lv_obj_set_style_text_color(countLbl, lv_color_hex(m3TextSecondaryHex()), 0);
    lv_obj_set_style_text_font(countLbl, &lv_font_montserrat_10, 0);

    // Scrollable note list container
    lv_obj_t* listCont = lv_obj_create(s_body);
    lv_obj_set_width(listCont, LV_PCT(100));
    lv_obj_set_flex_grow(listCont, 1);
    lv_obj_set_style_bg_opa(listCont, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(listCont, 0, 0);
    lv_obj_set_style_pad_all(listCont, 0, 0);
    lv_obj_set_flex_flow(listCont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(listCont, 6, 0);
    lv_obj_set_scroll_dir(listCont, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(listCont, LV_SCROLLBAR_MODE_AUTO);

    for (uint8_t i = 0; i < cnt; ++i) {
        const NoteItem* item = notesStoreGet(i);
        if (!item) continue;

        lv_obj_t* card = m3Card(listCont, LV_PCT(100), 46, 12);
        lv_obj_set_style_pad_hor(card, 10, 0);
        lv_obj_set_style_pad_ver(card, 6, 0);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, openNoteCb, LV_EVENT_CLICKED, (void*)(intptr_t)i);

        lv_obj_t* titleLbl = lv_label_create(card);
        lv_label_set_text(titleLbl, item->title);
        lv_label_set_long_mode(titleLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(titleLbl, 178);
        lv_obj_set_style_text_color(titleLbl, lv_color_hex(m3TextPrimaryHex()), 0);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_12, 0);
        lv_obj_align(titleLbl, LV_ALIGN_TOP_LEFT, 0, 0);

        char preview[48];
        makePreviewLine(item->body, preview, sizeof(preview));
        lv_obj_t* subLbl = lv_label_create(card);
        lv_label_set_text(subLbl, preview);
        lv_label_set_long_mode(subLbl, LV_LABEL_LONG_DOT);
        lv_obj_set_width(subLbl, 178);
        lv_obj_set_style_text_color(subLbl, lv_color_hex(m3TextSecondaryHex()), 0);
        lv_obj_set_style_text_font(subLbl, &lv_font_montserrat_10, 0);
        lv_obj_align(subLbl, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        lv_obj_t* chev = lv_label_create(card);
        lv_label_set_text(chev, LV_SYMBOL_RIGHT);
        lv_obj_set_style_text_color(chev, lv_color_hex(amberAccentHex()), 0);
        lv_obj_set_style_text_font(chev, &lv_font_montserrat_12, 0);
        lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
    }
}

static void renderEditorView(const char* initTitle, const char* initBody) {
    if (!s_body) return;
    clearEditorPtrs();
    lv_obj_clean(s_body);

    lv_obj_clear_flag(s_body, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(s_body, 6, 0);

    // Title textarea (visible above keyboard when typing)
    s_titleTa = lv_textarea_create(s_body);
    lv_obj_set_size(s_titleTa, LV_PCT(100), 32);
    lv_textarea_set_one_line(s_titleTa, true);
    lv_textarea_set_max_length(s_titleTa, (uint32_t)(NOTES_TITLE_MAX - 1));
    lv_textarea_set_placeholder_text(s_titleTa, "Title...");
    lv_textarea_set_text(s_titleTa, initTitle ? initTitle : "");
    styleTextarea(s_titleTa);
    m3KeyboardAttach(s_titleTa, onTitleEnter);

    // Multiline body textarea
    s_bodyTa = lv_textarea_create(s_body);
    lv_obj_set_width(s_bodyTa, LV_PCT(100));
    lv_obj_set_flex_grow(s_bodyTa, 1);
    lv_textarea_set_max_length(s_bodyTa, (uint32_t)(NOTES_BODY_MAX - 1));
    lv_textarea_set_placeholder_text(s_bodyTa, "Write a note...");
    lv_textarea_set_text(s_bodyTa, initBody ? initBody : "");
    styleTextarea(s_bodyTa);
    m3KeyboardAttach(s_bodyTa, onBodyEnter);

    // Bottom action toolbar: Back | Save | Del
    lv_obj_t* bar = lv_obj_create(s_body);
    lv_obj_set_size(bar, LV_PCT(100), 32);
    lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bar, 0, 0);
    lv_obj_set_style_pad_all(bar, 0, 0);
    lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(bar, 6, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

    s_backBtn = m3PillBtn(bar, LV_SYMBOL_LEFT, m3SurfaceHighestHex(), m3TextPrimaryHex(), 42, 32);
    lv_obj_add_event_cb(s_backBtn, backToListCb, LV_EVENT_CLICKED, nullptr);

    s_saveBtn = m3PillBtn(bar, LV_SYMBOL_SAVE " Save", amberAccentHex(), amberBtnFgHex(), 0, 32);
    lv_obj_set_flex_grow(s_saveBtn, 1);
    lv_obj_add_event_cb(s_saveBtn, saveBtnCb, LV_EVENT_CLICKED, nullptr);

    s_delBtn = m3PillBtn(bar, LV_SYMBOL_TRASH " Del", dangerBgHex(), dangerFgHex(), 66, 32);
    lv_obj_add_event_cb(s_delBtn, delBtnCb, LV_EVENT_CLICKED, nullptr);
}

static void refreshAppNotesTheme() {
    if (!s_body) return;
    if (s_view == NotesView::LIST) {
        renderListView();
        return;
    }

    // Editor view: re-skin in place so unsaved text is preserved
    styleTextarea(s_titleTa);
    styleTextarea(s_bodyTa);

    if (s_backBtn) {
        lv_obj_set_style_bg_color(s_backBtn, lv_color_hex(m3SurfaceHighestHex()), 0);
        lv_obj_t* lbl = lv_obj_get_child(s_backBtn, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(m3TextPrimaryHex()), 0);
    }
    if (s_saveBtn) {
        lv_obj_set_style_bg_color(s_saveBtn, lv_color_hex(amberAccentHex()), 0);
        lv_obj_t* lbl = lv_obj_get_child(s_saveBtn, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(amberBtnFgHex()), 0);
    }
    if (s_delBtn) {
        lv_obj_set_style_bg_color(s_delBtn, lv_color_hex(dangerBgHex()), 0);
        lv_obj_t* lbl = lv_obj_get_child(s_delBtn, 0);
        if (lbl) lv_obj_set_style_text_color(lbl, lv_color_hex(dangerFgHex()), 0);
    }
}

void buildAppNotes(lv_obj_t* body) {
    s_body = body;
    s_view = NotesView::LIST;
    s_editingIdx = -1;
    clearEditorPtrs();
    lv_obj_add_event_cb(body, notesDeleteCb, LV_EVENT_DELETE, nullptr);
    renderListView();
}

REGISTER_APP_THEMED(notes, "notes", "Notes", LV_SYMBOL_EDIT, 0xFDD663, 0xEA8C00,
                    buildAppNotes, nullptr, refreshAppNotesTheme, 110);
