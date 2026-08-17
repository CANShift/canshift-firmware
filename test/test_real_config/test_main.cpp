#include "board_config.h"
#include "config/config_loader.h"
#include "hal/storage/storage_driver.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unity.h>

static char *readFile(const char *path, size_t *lenOut) {
    FILE *f = fopen(path, "rb");
    if (!f)
        return nullptr;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (len < 0) {
        fclose(f);
        return nullptr;
    }
    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) {
        fclose(f);
        return nullptr;
    }
    const size_t got = fread(buf, 1, (size_t)len, f);
    fclose(f);
    if (got != (size_t)len) {
        free(buf);
        return nullptr;
    }
    buf[len] = '\0';
    if (lenOut)
        *lenOut = (size_t)len;
    return buf;
}

void setUp() {}
void tearDown() {}

void test_real_six_page_dashboard_loads() {
    size_t dashLen = 0, sigLen = 0;
    char *dash = readFile("data/config/dashboard.json", &dashLen);
    char *sigs = readFile("data/config/signals.json", &sigLen);
    TEST_ASSERT_NOT_NULL(dash);
    TEST_ASSERT_NOT_NULL(sigs);
    printf("dashboard.json bytes=%zu signals.json bytes=%zu\n", dashLen, sigLen);

    StorageDriver::fakeReset();
    StorageDriver::fakeWrite(CONFIG_PATH_DASHBOARD, dash, dashLen);
    StorageDriver::fakeWrite(CONFIG_PATH_SIGNALS, sigs, sigLen);

    const ConfigLoader::LoadResult result = ConfigLoader::loadAll();
    printf("dashboardOk=%d signalsOk=%d\n", result.dashboardOk ? 1 : 0, result.signalsOk ? 1 : 0);

    const CfgDashboard &d = ConfigLoader::getDashboardConfig();
    printf("loaded=%d pageCount=%u defaultPageId=%s\n", d.loaded ? 1 : 0, (unsigned)d.pageCount,
           d.defaultPageId);
    for (uint8_t i = 0; i < d.pageCount; ++i)
        printf("page[%u] id=%s visible=%d widgets=%u\n", static_cast<unsigned>(i), d.pages[i].id,
               d.pages[i].visible ? 1 : 0, (unsigned)d.pages[i].widgetCount);

    TEST_ASSERT_TRUE(result.dashboardOk);
    TEST_ASSERT_TRUE(result.signalsOk);
    TEST_ASSERT_TRUE(d.loaded);
    TEST_ASSERT_EQUAL_UINT8(6, d.pageCount);
    free(dash);
    free(sigs);
}

static bool signalIsDeclared(const CfgSignalConfig &s, const char *name) {
    for (uint8_t i = 0; i < s.signalCount; ++i) {
        if (strcmp(s.signals[i].name, name) == 0)
            return true;
    }
    return false;
}

void test_every_bound_signal_is_declared() {
    const CfgDashboard &d = ConfigLoader::getDashboardConfig();
    const CfgSignalConfig &s = ConfigLoader::getSignalConfig();
    printf("signalCount=%u cap=%u\n", (unsigned)s.signalCount, (unsigned)CONFIG_MAX_SIGNALS);
    TEST_ASSERT_LESS_OR_EQUAL_UINT8(CONFIG_MAX_SIGNALS, s.signalCount);

    for (uint8_t p = 0; p < d.pageCount; ++p) {
        for (uint8_t w = 0; w < d.pages[p].widgetCount; ++w) {
            const char *signal = d.pages[p].widgets[w].signalId;
            if (signal[0] == '\0')
                continue;
            printf("page[%u] widget[%u] signal=%s\n", (unsigned)p, (unsigned)w, signal);
            TEST_ASSERT_TRUE_MESSAGE(signalIsDeclared(s, signal), signal);
        }
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_real_six_page_dashboard_loads);
    RUN_TEST(test_every_bound_signal_is_declared);
    return UNITY_END();
}
