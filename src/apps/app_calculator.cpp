#include "app_registry.h"
#include "../theme/m3_theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static lv_obj_t* s_dispLbl = nullptr;
static char s_expr[24] = "0";
static double s_acc = 0.0;
static char s_op = 0;
static bool s_newEntry = true;

static void updateDisp() {
    if (s_dispLbl) lv_label_set_text(s_dispLbl, s_expr);
}

static void evalOp() {
    double rhs = atof(s_expr);
    if (s_op == '+') s_acc += rhs;
    else if (s_op == '-') s_acc -= rhs;
    else if (s_op == '*') s_acc *= rhs;
    else if (s_op == '/') s_acc = (rhs != 0.0) ? (s_acc / rhs) : 0.0;
    else s_acc = rhs;

    snprintf(s_expr, sizeof(s_expr), "%.6g", s_acc);
}

static void calcBtnCb(lv_event_t* e) {
    lv_obj_t* bm = lv_event_get_target(e);
    uint16_t id = lv_btnmatrix_get_selected_btn(bm);
    const char* txt = lv_btnmatrix_get_btn_text(bm, id);
    if (!txt) return;

    m3PulseFeedback();

    if (strcmp(txt, "C") == 0) {
        strcpy(s_expr, "0");
        s_acc = 0.0;
        s_op = 0;
        s_newEntry = true;
    } else if (strcmp(txt, "<") == 0) {
        size_t len = strlen(s_expr);
        if (len > 1) s_expr[len - 1] = '\0';
        else strcpy(s_expr, "0");
    } else if (strcmp(txt, "=") == 0) {
        evalOp();
        s_op = 0;
        s_newEntry = true;
    } else if (txt[0] == '+' || txt[0] == '-' || txt[0] == '*' || txt[0] == '/') {
        if (!s_newEntry && s_op) evalOp();
        else s_acc = atof(s_expr);
        s_op = txt[0];
        s_newEntry = true;
    } else {
        // Digit or dot
        if (s_newEntry) {
            if (txt[0] == '.') strcpy(s_expr, "0.");
            else snprintf(s_expr, sizeof(s_expr), "%s", txt);
            s_newEntry = false;
        } else if (strlen(s_expr) < 14) {
            if (txt[0] == '.' && strchr(s_expr, '.')) return;
            if (strcmp(s_expr, "0") == 0 && txt[0] != '.') s_expr[0] = '\0';
            strcat(s_expr, txt);
        }
    }
    updateDisp();
}

static const char* s_btnMap[] = {
    "C", "<", "/", "*", "\n",
    "7", "8", "9", "-", "\n",
    "4", "5", "6", "+", "\n",
    "1", "2", "3", "=", "\n",
    "0", ".", "", ""
};

void buildAppCalculator(lv_obj_t* body) {
    lv_obj_clear_flag(body, LV_OBJ_FLAG_SCROLLABLE);
    strcpy(s_expr, "0");
    s_acc = 0.0;
    s_op = 0;
    s_newEntry = true;

    // Display card
    lv_obj_t* disp = lv_obj_create(body);
    lv_obj_set_size(disp, LV_PCT(100), 48);
    lv_obj_align(disp, LV_ALIGN_TOP_MID, 0, 0);
    lv_obj_set_style_bg_color(disp, lv_color_hex(M3Color::SURFACE), 0);
    lv_obj_set_style_border_width(disp, 0, 0);
    lv_obj_set_style_radius(disp, 14, 0);
    lv_obj_set_style_pad_hor(disp, 12, 0);
    lv_obj_clear_flag(disp, LV_OBJ_FLAG_SCROLLABLE);

    s_dispLbl = lv_label_create(disp);
    lv_label_set_text(s_dispLbl, "0");
    lv_obj_set_style_text_color(s_dispLbl, lv_color_hex(M3Color::TEXT_PRIMARY), 0);
    lv_obj_set_style_text_font(s_dispLbl, &lv_font_montserrat_24, 0);
    lv_obj_align(s_dispLbl, LV_ALIGN_RIGHT_MID, 0, 0);

    // Button matrix
    lv_obj_t* bm = lv_btnmatrix_create(body);
    lv_btnmatrix_set_map(bm, s_btnMap);
    lv_obj_set_size(bm, LV_PCT(100), 162);
    lv_obj_align(bm, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_opa(bm, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(bm, 0, 0);
    lv_obj_set_style_pad_all(bm, 0, 0);
    lv_obj_set_style_pad_row(bm, 4, 0);
    lv_obj_set_style_pad_column(bm, 4, 0);

    lv_obj_set_style_bg_color(bm, lv_color_hex(M3Color::SURFACE_HIGH), LV_PART_ITEMS);
    lv_obj_set_style_text_color(bm, lv_color_hex(M3Color::TEXT_PRIMARY), LV_PART_ITEMS);
    lv_obj_set_style_radius(bm, 12, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(bm, 0, LV_PART_ITEMS);

    lv_btnmatrix_set_btn_ctrl(bm, 16, LV_BTNMATRIX_CTRL_HIDDEN);
    lv_btnmatrix_set_btn_ctrl(bm, 17, LV_BTNMATRIX_CTRL_HIDDEN);
    lv_btnmatrix_set_btn_width(bm, 14, 2);

    lv_obj_add_event_cb(bm, calcBtnCb, LV_EVENT_VALUE_CHANGED, nullptr);
}

REGISTER_APP(calculator, "calculator", "Calc", LV_SYMBOL_PLUS, 0xC5B4E3, 0x3A2F52, buildAppCalculator, nullptr, 70);
