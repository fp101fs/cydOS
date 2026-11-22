// Host-side unit test for App Store pure helpers (tabs, storage summary, installed app metadata).
// Build & run:
//   c++ -std=c++17 -Wall -Wextra tools/test_app_store.cpp -o /tmp/test_app_store && /tmp/test_app_store

#define APP_STORE_HOST_TEST
#include "../src/apps/app_store.cpp"

#include <cassert>
#include <cstdio>
#include <cstring>

static void testTabs() {
    assert(strcmp(appStoreTabLabel(AppStoreTab::Explore), "Explore") == 0);
    assert(strcmp(appStoreTabLabel(AppStoreTab::SdCard), "SD Card") == 0);
    assert(strcmp(appStoreTabLabel(AppStoreTab::Installed), "Installed") == 0);
    assert(strcmp(appStoreTabLabel(AppStoreTab::Tools), "Tools") == 0);

    assert(appStoreTabFromIndex(-5) == AppStoreTab::Explore);
    assert(appStoreTabFromIndex(0) == AppStoreTab::Explore);
    assert(appStoreTabFromIndex(1) == AppStoreTab::SdCard);
    assert(appStoreTabFromIndex(2) == AppStoreTab::Installed);
    assert(appStoreTabFromIndex(3) == AppStoreTab::Tools);
    assert(appStoreTabFromIndex(99) == AppStoreTab::Tools);
}

static void testStorageSummary() {
    // Empty 4 MB dynamic pool (0x1F0000 = 2,031,616 B)
    AppStoreStorageSummary empty = appStoreComputeStorageSummary(0x1F0000u, 0x1F0000u, 0);
    assert(empty.dynamicTotalBytes == 0x1F0000u);
    assert(empty.usedBytes == 0);
    assert(empty.freeBytes == 0x1F0000u);
    assert(empty.usedPct == 0);
    assert(empty.installedCount == 0);

    // Half used
    AppStoreStorageSummary half = appStoreComputeStorageSummary(2000000u, 1000000u, 2);
    assert(half.usedBytes == 1000000u);
    assert(half.freeBytes == 1000000u);
    assert(half.usedPct == 50);
    assert(half.installedCount == 2);

    // Guard against freeBytes > dynamicTotalBytes
    AppStoreStorageSummary clamped = appStoreComputeStorageSummary(1000u, 5000u, 1);
    assert(clamped.freeBytes == 1000u);
    assert(clamped.usedBytes == 0);
    assert(clamped.usedPct == 0);

    // Guard against dynamicTotalBytes == 0
    AppStoreStorageSummary zero = appStoreComputeStorageSummary(0, 0, 0);
    assert(zero.usedPct == 0);
    assert(zero.usedBytes == 0);
}

static void testSizeFormatting() {
    char buf[32];
    appStoreFormatSizeCompact(512u, buf, sizeof(buf));
    assert(strcmp(buf, "512 B") == 0);

    appStoreFormatSizeCompact(65536u, buf, sizeof(buf));
    assert(strcmp(buf, "64 KB") == 0);

    appStoreFormatSizeCompact(1024u * 1024u, buf, sizeof(buf));
    assert(strcmp(buf, "1.0 MB") == 0);

    appStoreFormatSizeCompact(1572864u, buf, sizeof(buf));
    assert(strcmp(buf, "1.5 MB") == 0);
}

static void testInstalledMetaFormatting() {
    char buf[64];

    appStoreFormatInstalledMeta(524288u, 0, false, false, buf, sizeof(buf));
    assert(strcmp(buf, "512 KB") == 0);

    appStoreFormatInstalledMeta(1258291u, 1, false, false, buf, sizeof(buf));
    assert(strcmp(buf, "1.2 MB • 1 data") == 0);

    appStoreFormatInstalledMeta(1048576u, 2, true, true, buf, sizeof(buf));
    assert(strcmp(buf, "1.0 MB • 2 data • Backup • Boot") == 0);

    appStoreFormatInstalledMeta(917504u, 0, true, false, buf, sizeof(buf));
    assert(strcmp(buf, "896 KB • Boot") == 0);
}

int main() {
    testTabs();
    testStorageSummary();
    testSizeFormatting();
    testInstalledMetaFormatting();
    printf("[OK] test_app_store: all assertions passed\n");
    return 0;
}
