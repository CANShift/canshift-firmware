#include "runtime/page_broadcast.h"

#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

namespace {

PageBroadcast::Snapshot snapshotOf(const char *const *ids, uint8_t count, uint8_t current) {
    PageBroadcast::Snapshot snapshot = {};
    snapshot.count = count;
    snapshot.current = current;
    for (uint8_t i = 0; i < count; ++i)
        strlcpy(snapshot.ids[i], ids[i], kCfgMaxIdLen);
    return snapshot;
}

constexpr const char *kStreetTrack[] = {"street", "track"};

}

void setUp() {
    PageBroadcast::publish(snapshotOf(nullptr, 0, PageBroadcast::kNoCurrentPage));
}

void tearDown() {}

void test_latestReturnsThePublishedSnapshot() {
    PageBroadcast::publish(snapshotOf(kStreetTrack, 2, 1));
    const PageBroadcast::Snapshot latest = PageBroadcast::latest();
    TEST_ASSERT_EQUAL_UINT8(2, latest.count);
    TEST_ASSERT_EQUAL_STRING("track", PageBroadcast::currentId(latest));
}

void test_currentIdIsEmptyWithoutPages() {
    TEST_ASSERT_EQUAL_STRING("", PageBroadcast::currentId(PageBroadcast::latest()));
}

void test_currentIdIsEmptyWhenTheIndexIsOutOfRange() {
    TEST_ASSERT_EQUAL_STRING("", PageBroadcast::currentId(snapshotOf(kStreetTrack, 2, 2)));
}

void test_pageListKeepsTheDashOrder() {
    JsonDocument doc;
    PageBroadcast::writePageList(doc.to<JsonObject>(), snapshotOf(kStreetTrack, 2, 0));
    char buf[64];
    serializeJson(doc, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"pages\":[\"street\",\"track\"]}", buf);
}

void test_emptyPageListStillCarriesTheArray() {
    JsonDocument doc;
    PageBroadcast::writePageList(doc.to<JsonObject>(), PageBroadcast::latest());
    char buf[32];
    serializeJson(doc, buf, sizeof(buf));
    TEST_ASSERT_EQUAL_STRING("{\"pages\":[]}", buf);
}

void test_fullPageListFitsTheBlePayloadCap() {
    PageBroadcast::Snapshot snapshot = {};
    snapshot.count = kConfigMaxPages;
    for (uint8_t i = 0; i < kConfigMaxPages; ++i) {
        memset(snapshot.ids[i], 'p', kCfgMaxIdLen - 1);
        snapshot.ids[i][kCfgMaxIdLen - 1] = '\0';
    }
    JsonDocument doc;
    PageBroadcast::writePageList(doc.to<JsonObject>(), snapshot);
    TEST_ASSERT_LESS_THAN(320, measureJson(doc));
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_latestReturnsThePublishedSnapshot);
    RUN_TEST(test_currentIdIsEmptyWithoutPages);
    RUN_TEST(test_currentIdIsEmptyWhenTheIndexIsOutOfRange);
    RUN_TEST(test_pageListKeepsTheDashOrder);
    RUN_TEST(test_emptyPageListStillCarriesTheArray);
    RUN_TEST(test_fullPageListFitsTheBlePayloadCap);
    return UNITY_END();
}
