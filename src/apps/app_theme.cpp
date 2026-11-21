#include "app_registry.h"
#include "../theme/m3_theme.h"
#include "../components/m3_widgets.h"

static void toggleThemeAction() {
    g_sys.lightMode = !g_sys.lightMode;
    m3ApplyThemeAll();
    m3ShowToast(g_sys.lightMode ? "Light Mode" : "Dark Mode", 1100);
}

REGISTER_APP_EX(theme, "theme", "Theme", LV_SYMBOL_EYE_OPEN, 0xFDD663, 0x453917, nullptr, nullptr, 130, toggleThemeAction);
