#include "app_registry.h"
#include <string.h>

// Zero-initialized in BSS before any C++ static constructors run
static const AppMeta* s_apps[APP_MAX_COUNT];
static uint8_t s_appCount = 0;
static bool s_sorted = false;

static int compareAppMeta(const AppMeta* a, const AppMeta* b) {
    if (a->order != b->order) {
        return (a->order < b->order) ? -1 : 1;
    }
    const char* nameA = a->name ? a->name : "";
    const char* nameB = b->name ? b->name : "";
    int cmp = strcmp(nameA, nameB);
    if (cmp != 0) return cmp;
    const char* idA = a->id ? a->id : "";
    const char* idB = b->id ? b->id : "";
    return strcmp(idA, idB);
}

static void ensureSorted() {
    if (s_sorted) return;
    // Insertion sort (small N, zero heap allocation)
    for (uint8_t i = 1; i < s_appCount; ++i) {
        const AppMeta* key = s_apps[i];
        int j = (int)i - 1;
        while (j >= 0 && compareAppMeta(s_apps[j], key) > 0) {
            s_apps[j + 1] = s_apps[j];
            --j;
        }
        s_apps[j + 1] = key;
    }
    s_sorted = true;
}

AppRegistrar::AppRegistrar(const AppMeta* meta) {
    appRegister(meta);
}

void appRegister(const AppMeta* meta) {
    if (!meta || !meta->id || s_appCount >= APP_MAX_COUNT) return;
    for (uint8_t i = 0; i < s_appCount; ++i) {
        if (s_apps[i] && s_apps[i]->id && strcmp(s_apps[i]->id, meta->id) == 0) {
            return;
        }
    }
    s_apps[s_appCount++] = meta;
    s_sorted = false;
}

uint8_t appGetCount() {
    ensureSorted();
    return s_appCount;
}

const AppMeta* appGetAt(uint8_t index) {
    ensureSorted();
    if (index >= s_appCount) return nullptr;
    return s_apps[index];
}

const AppMeta* appFindById(const char* id) {
    if (!id) return nullptr;
    ensureSorted();
    for (uint8_t i = 0; i < s_appCount; ++i) {
        if (s_apps[i] && s_apps[i]->id && strcmp(s_apps[i]->id, id) == 0) {
            return s_apps[i];
        }
    }
    return nullptr;
}
