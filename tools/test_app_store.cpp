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

static void testExploreHelpers() {
    assert(strcmp(appStoreSortLabel(AppStoreSort::Popular), "Popular") == 0);
    assert(strcmp(appStoreSortLabel(AppStoreSort::Recent), "Recent") == 0);
    assert(strcmp(appStoreSortLabel(AppStoreSort::Name), "A-Z") == 0);
    assert(strcmp(appStoreSortLabel(AppStoreSort::Starred), "Starred") == 0);

    assert(strcmp(appStoreSortApiOrder(AppStoreSort::Popular), "downloads") == 0);
    assert(strcmp(appStoreSortApiOrder(AppStoreSort::Recent), "published_at") == 0);
    assert(strcmp(appStoreSortApiOrder(AppStoreSort::Name), "name") == 0);
    assert(strcmp(appStoreSortApiOrder(AppStoreSort::Starred), "downloads") == 0);

    assert(!appStoreSortStarOnly(AppStoreSort::Popular));
    assert(!appStoreSortStarOnly(AppStoreSort::Recent));
    assert(!appStoreSortStarOnly(AppStoreSort::Name));
    assert(appStoreSortStarOnly(AppStoreSort::Starred));

    assert(appStoreSortFromIndex(-1) == AppStoreSort::Popular);
    assert(appStoreSortFromIndex(0) == AppStoreSort::Popular);
    assert(appStoreSortFromIndex(1) == AppStoreSort::Recent);
    assert(appStoreSortFromIndex(2) == AppStoreSort::Name);
    assert(appStoreSortFromIndex(3) == AppStoreSort::Starred);
    assert(appStoreSortFromIndex(99) == AppStoreSort::Starred);

    char buf[64];
    appStoreFormatPageLabel(1, 3, 42, buf, sizeof(buf));
    assert(strcmp(buf, "Page 1/3 (42)") == 0);
    appStoreFormatPageLabel(5, 2, 0, buf, sizeof(buf));
    assert(strcmp(buf, "Page 2/2") == 0);
    appStoreFormatPageLabel(0, 0, 10, buf, sizeof(buf));
    assert(strcmp(buf, "Page 1/1 (10)") == 0);

    appStoreFormatVersionMeta(1339872u, 1405408u, "2026-11-06T12:00:00Z", false, buf, sizeof(buf));
    assert(strcmp(buf, "1.3 MB • 2026-11-06") == 0);
    appStoreFormatVersionMeta(0u, 524288u, "2026-10-01", true, buf, sizeof(buf));
    assert(strcmp(buf, "512 KB • 2026-10-01 • +Data") == 0);
    appStoreFormatVersionMeta(0u, 0u, nullptr, false, buf, sizeof(buf));
    assert(strcmp(buf, "Ready to install") == 0);

    assert(appStoreIsValidInstallUrl("https://example.com/app.bin"));
    assert(appStoreIsValidInstallUrl("  http://192.168.1.50:8080/fw.bin  "));
    assert(!appStoreIsValidInstallUrl(""));
    assert(!appStoreIsValidInstallUrl(nullptr));
    assert(!appStoreIsValidInstallUrl("ftp://example.com/app.bin"));
    assert(!appStoreIsValidInstallUrl("https://"));
    assert(!appStoreIsValidInstallUrl("https://bad host/app.bin"));
    assert(!appStoreIsValidInstallUrl("https://nodot"));
}

int main() {
    testTabs();
    testStorageSummary();
    testSizeFormatting();
    testInstalledMetaFormatting();
    testExploreHelpers();
    printf("[OK] test_app_store: all assertions passed\n");
    return 0;
}
