#include "app_registry.h"
#include <string.h>

struct DynamicAppSlot {
    bool used;
    AppMeta meta;
    char idBuf[24];
    char nameBuf[24];
    char labelBuf[16];
};

// Zero-initialized in BSS before any C++ static constructors run
static const AppMeta* s_apps[APP_MAX_COUNT];
static uint8_t s_appCount = 0;
static bool s_sorted = false;
static DynamicAppSlot s_dynSlots[APP_MAX_DYNAMIC];
static AppDynamicLaunchFn s_dynLaunchFn = nullptr;

static void copyStr(char* dst, size_t dstSize, const char* src) {
    if (!dst || dstSize == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, src, dstSize - 1);
    dst[dstSize - 1] = '\0';
}

bool appIsDynamic(const AppMeta* meta) {
    return meta && meta->launcherLabel && meta->launcherLabel[0] != '\0';
}

// Sort priority bucket:
//   0: Built-in apps with order <= 0 (App Store at order = 0 -> always Home Screen position #1)
//   1: Runtime-installed dynamic Launcher Apps (ordered newest-first right after App Store)
//   2: Built-in apps with order > 0 (Files=10, Gallery=20, ...)
static int appSortBucket(const AppMeta* meta) {
    if (appIsDynamic(meta)) return 1;
    return (meta->order <= 0) ? 0 : 2;
}

static int compareAppMeta(const AppMeta* a, const AppMeta* b) {
    int bucketA = appSortBucket(a);
    int bucketB = appSortBucket(b);
    if (bucketA != bucketB) {
        return (bucketA < bucketB) ? -1 : 1;
    }
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

static DynamicAppSlot* findDynamicSlotById(const char* id) {
    if (!id || !id[0]) return nullptr;
    for (uint8_t i = 0; i < APP_MAX_DYNAMIC; ++i) {
        if (s_dynSlots[i].used && strcmp(s_dynSlots[i].idBuf, id) == 0) {
            return &s_dynSlots[i];
        }
    }
    return nullptr;
}

static DynamicAppSlot* allocDynamicSlot() {
    for (uint8_t i = 0; i < APP_MAX_DYNAMIC; ++i) {
        if (!s_dynSlots[i].used) {
            memset(&s_dynSlots[i], 0, sizeof(DynamicAppSlot));
            s_dynSlots[i].used = true;
            return &s_dynSlots[i];
        }
    }
    return nullptr;
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

const AppMeta* appRegisterDynamic(const char* id,
                                  const char* name,
                                  const char* launcherLabel,
                                  const char* icon,
                                  uint32_t accentHex,
                                  uint32_t containerHex,
                                  int16_t order) {
    if (!id || !id[0] || !launcherLabel || !launcherLabel[0]) return nullptr;

    DynamicAppSlot* slot = findDynamicSlotById(id);
    const bool isNew = (slot == nullptr);
    if (isNew) {
        // Prevent colliding with a built-in app ID
        for (uint8_t i = 0; i < s_appCount; ++i) {
            if (s_apps[i] && s_apps[i]->id && strcmp(s_apps[i]->id, id) == 0) {
                return nullptr;
            }
        }
        if (s_appCount >= APP_MAX_COUNT) return nullptr;
        slot = allocDynamicSlot();
        if (!slot) return nullptr;
    }

    copyStr(slot->idBuf, sizeof(slot->idBuf), id);
    copyStr(slot->nameBuf, sizeof(slot->nameBuf), (name && name[0]) ? name : launcherLabel);
    copyStr(slot->labelBuf, sizeof(slot->labelBuf), launcherLabel);

    slot->meta.id            = slot->idBuf;
    slot->meta.name          = slot->nameBuf;
    slot->meta.icon          = (icon && icon[0]) ? icon : LV_SYMBOL_PLAY;
    slot->meta.accentHex     = accentHex;
    slot->meta.containerHex  = containerHex;
    slot->meta.buildFn       = nullptr;
    slot->meta.tickFn        = nullptr;
    slot->meta.order         = order;
    slot->meta.actionFn      = nullptr;
    slot->meta.themeFn       = nullptr;
    slot->meta.iconImg       = nullptr;
    slot->meta.launcherLabel = slot->labelBuf;

    if (isNew) {
        s_apps[s_appCount++] = &slot->meta;
    }
    s_sorted = false;
    return &slot->meta;
}

bool appUnregisterById(const char* id) {
    if (!id || !id[0]) return false;
    DynamicAppSlot* slot = findDynamicSlotById(id);
    if (!slot) return false; // Only dynamic apps can be unregistered at runtime

    bool removed = false;
    for (uint8_t i = 0; i < s_appCount; ++i) {
        if (s_apps[i] == &slot->meta) {
            for (uint8_t j = i; j + 1 < s_appCount; ++j) {
                s_apps[j] = s_apps[j + 1];
            }
            s_apps[--s_appCount] = nullptr;
            removed = true;
            break;
        }
    }
    memset(slot, 0, sizeof(DynamicAppSlot));
    s_sorted = false;
    return removed;
}

void appClearDynamic() {
    uint8_t writeIdx = 0;
    for (uint8_t i = 0; i < s_appCount; ++i) {
        if (s_apps[i] && !appIsDynamic(s_apps[i])) {
            s_apps[writeIdx++] = s_apps[i];
        }
    }
    for (uint8_t i = writeIdx; i < s_appCount; ++i) {
        s_apps[i] = nullptr;
    }
    s_appCount = writeIdx;
    memset(s_dynSlots, 0, sizeof(s_dynSlots));
    s_sorted = false;
}

uint8_t appGetDynamicCount() {
    uint8_t count = 0;
    for (uint8_t i = 0; i < APP_MAX_DYNAMIC; ++i) {
        if (s_dynSlots[i].used) ++count;
    }
    return count;
}

void appSetDynamicLaunchHandler(AppDynamicLaunchFn fn) {
    s_dynLaunchFn = fn;
}

AppDynamicLaunchFn appGetDynamicLaunchHandler() {
    return s_dynLaunchFn;
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
