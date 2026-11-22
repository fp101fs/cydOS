// Host-side unit test for Item 5: dynamic runtime App tiles in app_registry
// and newest-first Home Screen slot #1 ordering in launcher_home_sync.
//
// Compile & run:
//   c++ -std=c++17 -Wall -Wextra -Wno-unused-parameter tools/test_dynamic_apps.cpp -o /tmp/test_dynamic_apps && /tmp/test_dynamic_apps

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Minimal LVGL stubs for compiling src/apps/app_registry.cpp on host
struct lv_obj_t {};
struct lv_img_dsc_t {};
#define LV_SYMBOL_PLAY "\xEF\x81\x8B"
#define LV_SYMBOL_DOWNLOAD "\xEF\x80\x99"

// Include real implementations directly
#define APP_REGISTRY_HOST_TEST
#include "../src/apps/app_registry.cpp"

#define LAUNCHER_HOME_SYNC_HOST_TEST
#include "../src/launcher/launcher_home_sync.cpp"

static void dummyBuild(lv_obj_t*) {}

static const AppMeta s_appStore = {
    "app_store", "App Store", LV_SYMBOL_DOWNLOAD, 0x8AB4F8, 0x1A73E8,
    dummyBuild, nullptr, 0, nullptr, nullptr, nullptr, nullptr
};
static const AppMeta s_files = {
    "files", "Files", "F", 0xFDD663, 0xE5A00D,
    dummyBuild, nullptr, 10, nullptr, nullptr, nullptr, nullptr
};
static const AppMeta s_gallery = {
    "gallery", "Gallery", "G", 0x81C995, 0x22A06B,
    dummyBuild, nullptr, 20, nullptr, nullptr, nullptr, nullptr
};
static const AppMeta s_settings = {
    "settings", "Settings", "S", 0x8AB4F8, 0x475569,
    dummyBuild, nullptr, 120, nullptr, nullptr, nullptr, nullptr
};

static const AppMeta* s_lastLaunched = nullptr;
static void testLaunchHandler(const AppMeta* meta) {
    s_lastLaunched = meta;
}

static void syncFromSimulatedApps(std::vector<std::string>& recencyOrder,
                                  const std::vector<std::pair<std::string, std::string>>& installed,
                                  const char* changedLabel,
                                  bool* outNewAdded = nullptr) {
    std::vector<std::string> labels;
    for (const auto& p : installed) labels.push_back(p.first);
    launcherHomeReconcileOrder(recencyOrder, labels, changedLabel, outNewAdded);

    appClearDynamic();
    int16_t ord = 1;
    for (const std::string& lbl : recencyOrder) {
        for (const auto& p : installed) {
            if (p.first == lbl) {
                char idBuf[24];
                launcherHomeMakeDynamicId(lbl.c_str(), idBuf, sizeof(idBuf));
                uint32_t acc = 0, cont = 0;
                launcherHomePickColors(lbl.c_str(), &acc, &cont);
                appRegisterDynamic(idBuf, p.second.c_str(), lbl.c_str(),
                                   LV_SYMBOL_PLAY, acc, cont, ord++);
                break;
            }
        }
    }
}

int main() {
    // 1. Register built-in apps in scrambled order; App Store (order = 0) must be index 0.
    appRegister(&s_settings);
    appRegister(&s_files);
    appRegister(&s_appStore);
    appRegister(&s_gallery);

    assert(appGetCount() == 4);
    assert(appGetDynamicCount() == 0);
    assert(strcmp(appGetAt(0)->id, "app_store") == 0);
    assert(strcmp(appGetAt(1)->id, "files") == 0);
    assert(strcmp(appGetAt(2)->id, "gallery") == 0);
    assert(strcmp(appGetAt(3)->id, "settings") == 0);
    assert(!appIsDynamic(appGetAt(0)));

    // 2. Install first Launcher App ("bruce") -> must land at Home Screen slot #1 (index 1, right next to App Store)
    std::vector<std::string> recency;
    std::vector<std::pair<std::string, std::string>> installed = {
        {"bruce", "Bruce"}
    };
    bool newAdded = false;
    syncFromSimulatedApps(recency, installed, "bruce", &newAdded);
    assert(newAdded == true);
    assert(appGetCount() == 5);
    assert(appGetDynamicCount() == 1);
    assert(strcmp(appGetAt(0)->id, "app_store") == 0);
    assert(strcmp(appGetAt(1)->id, "lapp_bruce") == 0);
    assert(strcmp(appGetAt(1)->name, "Bruce") == 0);
    assert(strcmp(appGetAt(1)->launcherLabel, "bruce") == 0);
    assert(appIsDynamic(appGetAt(1)));
    assert(strcmp(appGetAt(2)->id, "files") == 0);

    // 3. Install second Launcher App ("marauder") -> newest first! Marauder at slot #1, Bruce shifts to slot #2
    installed.push_back({"marauder", "Marauder"});
    newAdded = false;
    syncFromSimulatedApps(recency, installed, "marauder", &newAdded);
    assert(newAdded == true);
    assert(appGetCount() == 6);
    assert(appGetDynamicCount() == 2);
    assert(strcmp(appGetAt(0)->id, "app_store") == 0);
    assert(strcmp(appGetAt(1)->id, "lapp_marauder") == 0);
    assert(strcmp(appGetAt(2)->id, "lapp_bruce") == 0);
    assert(strcmp(appGetAt(3)->id, "files") == 0);

    // 4. Rename "bruce" -> "Bruce v2": must NOT reshuffle recency order or mark as new install
    installed[0].second = "Bruce v2";
    newAdded = false;
    syncFromSimulatedApps(recency, installed, "bruce", &newAdded);
    assert(newAdded == false);
    assert(strcmp(appGetAt(0)->id, "app_store") == 0);
    assert(strcmp(appGetAt(1)->id, "lapp_marauder") == 0);
    assert(strcmp(appGetAt(2)->id, "lapp_bruce") == 0);
    assert(strcmp(appGetAt(2)->name, "Bruce v2") == 0);

    // 5. External/WebUI install ("nemo") with changedLabel=nullptr -> auto-prepended to slot #1
    installed.push_back({"nemo", "Nemo"});
    newAdded = false;
    syncFromSimulatedApps(recency, installed, nullptr, &newAdded);
    assert(newAdded == true);
    assert(strcmp(appGetAt(0)->id, "app_store") == 0);
    assert(strcmp(appGetAt(1)->id, "lapp_nemo") == 0);
    assert(strcmp(appGetAt(2)->id, "lapp_marauder") == 0);
    assert(strcmp(appGetAt(3)->id, "lapp_bruce") == 0);
    assert(strcmp(appGetAt(4)->id, "files") == 0);

    // 6. Uninstall "marauder" -> pruned cleanly, remaining dynamic apps preserve newest-first
    installed.erase(installed.begin() + 1);
    newAdded = false;
    syncFromSimulatedApps(recency, installed, nullptr, &newAdded);
    assert(newAdded == false);
    assert(appGetDynamicCount() == 2);
    assert(strcmp(appGetAt(0)->id, "app_store") == 0);
    assert(strcmp(appGetAt(1)->id, "lapp_nemo") == 0);
    assert(strcmp(appGetAt(2)->id, "lapp_bruce") == 0);
    assert(strcmp(appGetAt(3)->id, "files") == 0);

    // 7. Dynamic launch handler & safety guards
    appSetDynamicLaunchHandler(testLaunchHandler);
    assert(appGetDynamicLaunchHandler() == testLaunchHandler);
    const AppMeta* nemo = appFindById("lapp_nemo");
    assert(nemo != nullptr);
    appGetDynamicLaunchHandler()(nemo);
    assert(s_lastLaunched == nemo);

    // Unregistering a built-in app must fail; unregistering a dynamic app must succeed
    assert(appUnregisterById("app_store") == false);
    assert(appUnregisterById("files") == false);
    assert(appUnregisterById("lapp_nemo") == true);
    assert(appFindById("lapp_nemo") == nullptr);
    assert(appGetDynamicCount() == 1);

    // 8. Stress test: 12 dynamic apps must ALL sit strictly between App Store (idx 0) and Files (order 10)
    appClearDynamic();
    assert(appGetDynamicCount() == 0);
    assert(appGetCount() == 4);
    for (int i = 1; i <= 12; ++i) {
        char id[24], name[24], lbl[16];
        snprintf(id, sizeof(id), "lapp_a%d", i);
        snprintf(name, sizeof(name), "App %d", i);
        snprintf(lbl, sizeof(lbl), "a%d", i);
        const AppMeta* m = appRegisterDynamic(id, name, lbl, LV_SYMBOL_PLAY, 0x8AB4F8, 0x2563EB, i);
        assert(m != nullptr);
    }
    assert(appGetDynamicCount() == 12);
    assert(appGetCount() == 16);
    assert(strcmp(appGetAt(0)->id, "app_store") == 0);
    assert(strcmp(appGetAt(1)->id, "lapp_a1") == 0);
    assert(strcmp(appGetAt(12)->id, "lapp_a12") == 0);
    assert(strcmp(appGetAt(13)->id, "files") == 0);

    // 9. Deterministic palette & ID helper checks
    uint32_t a1 = 0, c1 = 0, a2 = 0, c2 = 0;
    launcherHomePickColors("bruce", &a1, &c1);
    launcherHomePickColors("bruce", &a2, &c2);
    assert(a1 == a2 && c1 == c2 && a1 != 0 && c1 != 0);

    char idCheck[24];
    launcherHomeMakeDynamicId("app0", idCheck, sizeof(idCheck));
    assert(strcmp(idCheck, "lapp_app0") == 0);

    // Clear dynamic leaves built-ins intact
    appClearDynamic();
    assert(appGetCount() == 4);
    assert(strcmp(appGetAt(0)->id, "app_store") == 0);

    printf("[OK] All dynamic app registry & Home Screen ordering tests passed!\n");
    return 0;
}
