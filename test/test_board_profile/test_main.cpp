#include "config/board_profile_loader.h"

#include "boards/catalog.h"

#include <stdio.h>
#include <string.h>
#include <unity.h>

using namespace canshift::boards;

namespace {

char s_idBuf[kBoardIdCapacity];
char s_nameBuf[kBoardNameCapacity];

const char *buildBlobWithTouch(char *buf, size_t cap, const char *boardId, const char *lcdDriver,
                               int lcdPinCs, const char *touchDriver) {
    snprintf(
        buf, cap,
        "{\"magic\":\"CANSHIFT_BOARD\",\"schema\":\"board-profile\",\"formatVersion\":1,"
        "\"profile\":{\"board_id\":\"%s\",\"board_name\":\"Test Board\",\"chip_family\":"
        "\"esp32s3\",\"lcd\":{\"driver\":\"%s\",\"pin_mosi\":13,\"pin_miso\":12,\"pin_sclk\":14,"
        "\"pin_cs\":%d,\"pin_dc\":2,\"pin_rst\":-1,\"pin_bl\":27,\"freq_write_hz\":40000000,"
        "\"panel_width\":240,\"panel_height\":320,\"memory_width\":240,\"memory_height\":320,"
        "\"default_rotation\":3,\"rgb_order_bgr\":false,\"invert\":true,\"bus_shared_with_touch\":"
        "true,\"readable\":false,\"color_depth\":16},\"backlight\":{\"present\":true,\"pwm_"
        "channel\""
        ":0,\"pwm_freq_hz\":5000,\"default_duty\":200,\"invert\":false},\"touch\":{\"driver\":"
        "\"%s\",\"pin_cs\":-1,\"pin_irq\":-1,\"freq_hz\":400000,\"needs_calibration\":false,"
        "\"pin_sda\":21,\"pin_scl\":22},\"can\":{\"controller\":\"esp_twai\",\"pin_tx\":25,\"pin_"
        "rx\""
        ":32,\"default_speed_kbps\":500},\"storage\":{\"spiffs_present\":true,\"spiffs_size_kb\":"
        "1024,\"sd_present\":false,\"sd_pin_cs\":-1},\"conn\":{\"wifi_supported\":true,"
        "\"ble_supported\":true,\"psram_present\":true}}}",
        boardId, lcdDriver, lcdPinCs, touchDriver);
    return buf;
}

const char *buildBlob(char *buf, size_t cap, const char *boardId, const char *lcdDriver,
                      int lcdPinCs) {
    return buildBlobWithTouch(buf, cap, boardId, lcdDriver, lcdPinCs, "cst816s");
}

BoardProfileParse parse(const char *json, BoardProfile &out) {
    return parseBoardProfileBlob(json, strlen(json), out, s_idBuf, sizeof s_idBuf, s_nameBuf,
                                 sizeof s_nameBuf);
}

} // namespace

void setUp() {}
void tearDown() {
    resetRuntimeBoardProfile();
}

void test_validBlob_parsesAllFields() {
    char blob[1200];
    buildBlob(blob, sizeof blob, "test_board", "st7789", 15);
    BoardProfile out{};
    TEST_ASSERT_EQUAL(static_cast<int>(BoardProfileParse::Ok), static_cast<int>(parse(blob, out)));

    TEST_ASSERT_EQUAL_STRING("test_board", out.board_id);
    TEST_ASSERT_EQUAL_STRING("Test Board", out.board_name);
    TEST_ASSERT_EQUAL(static_cast<int>(ChipFamily::Esp32s3), static_cast<int>(out.chip_family));
    TEST_ASSERT_EQUAL(static_cast<int>(LcdDriver::ST7789), static_cast<int>(out.lcd.driver));
    TEST_ASSERT_EQUAL_INT(15, out.lcd.pin_cs);
    TEST_ASSERT_EQUAL_INT(27, out.lcd.pin_bl);
    TEST_ASSERT_EQUAL_UINT16(320, out.lcd.panel_height);
    TEST_ASSERT_EQUAL_UINT32(40000000u, out.lcd.freq_write_hz);
    TEST_ASSERT_TRUE(out.lcd.invert);
    TEST_ASSERT_TRUE(out.lcd.bus_shared_with_touch);
    TEST_ASSERT_EQUAL(static_cast<int>(TouchDriver::CST816S), static_cast<int>(out.touch.driver));
    TEST_ASSERT_EQUAL_INT(21, out.touch.pin_sda);
    TEST_ASSERT_FALSE(out.touch.needs_calibration);
    TEST_ASSERT_EQUAL(static_cast<int>(CanController::EspTwai),
                      static_cast<int>(out.can.controller));
    TEST_ASSERT_EQUAL_UINT16(500, out.can.default_speed_kbps);
    TEST_ASSERT_EQUAL_UINT16(1024, out.storage.spiffs_size_kb);
    TEST_ASSERT_TRUE(out.conn.psram_present);
}

void test_invalidJson_rejected() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(static_cast<int>(BoardProfileParse::InvalidJson),
                      static_cast<int>(parse("{oops", out)));
}

void test_nonObjectRoot_rejected() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(static_cast<int>(BoardProfileParse::NotAnObject),
                      static_cast<int>(parse("[1,2,3]", out)));
}

void test_wrongMagic_rejected() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(BoardProfileParse::WrongMagic),
        static_cast<int>(parse("{\"magic\":\"NOPE\",\"formatVersion\":1,\"profile\":{}}", out)));
}

void test_newerFormatVersion_rejected() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(BoardProfileParse::UnsupportedVersion),
        static_cast<int>(
            parse("{\"magic\":\"CANSHIFT_BOARD\",\"formatVersion\":2,\"profile\":{}}", out)));
}

void test_missingProfileFields_rejected() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(
        static_cast<int>(BoardProfileParse::WrongShape),
        static_cast<int>(
            parse("{\"magic\":\"CANSHIFT_BOARD\",\"formatVersion\":1,\"profile\":{}}", out)));
}

void test_outOfRangePin_rejected() {
    char blob[1200];
    buildBlob(blob, sizeof blob, "test_board", "st7789", 999);
    BoardProfile out{};
    TEST_ASSERT_EQUAL(static_cast<int>(BoardProfileParse::WrongShape),
                      static_cast<int>(parse(blob, out)));
}

void test_cst3530TouchSlug_parses() {
    char blob[1200];
    buildBlobWithTouch(blob, sizeof blob, "test_board", "st7789", 15, "cst3530");
    BoardProfile out{};
    TEST_ASSERT_EQUAL(static_cast<int>(BoardProfileParse::Ok), static_cast<int>(parse(blob, out)));
    TEST_ASSERT_EQUAL(static_cast<int>(TouchDriver::CST3530), static_cast<int>(out.touch.driver));
}

void test_unknownEnum_rejected() {
    char blob[1200];
    buildBlob(blob, sizeof blob, "test_board", "not_a_driver", 15);
    BoardProfile out{};
    TEST_ASSERT_EQUAL(static_cast<int>(BoardProfileParse::WrongShape),
                      static_cast<int>(parse(blob, out)));
}

void test_runtimeDefaultsToCompileTimeBoard() {
    resetRuntimeBoardProfile();
    TEST_ASSERT_EQUAL_STRING("crowpanel_28", runtimeBoardProfile().board_id);
    TEST_ASSERT_EQUAL(static_cast<int>(ChipFamily::Esp32),
                      static_cast<int>(runtimeBoardProfile().chip_family));
}

void test_applyValidBlob_overridesRuntimeProfile() {
    char blob[1200];
    buildBlob(blob, sizeof blob, "test_board", "st7789", 15);
    TEST_ASSERT_TRUE(applyBoardProfileBlob(blob, strlen(blob)));
    TEST_ASSERT_EQUAL_STRING("test_board", runtimeBoardProfile().board_id);
    TEST_ASSERT_EQUAL(static_cast<int>(ChipFamily::Esp32s3),
                      static_cast<int>(runtimeBoardProfile().chip_family));
}

void test_applyInvalidBlob_keepsPriorProfile() {
    char blob[1200];
    buildBlob(blob, sizeof blob, "test_board", "st7789", 15);
    TEST_ASSERT_TRUE(applyBoardProfileBlob(blob, strlen(blob)));
    TEST_ASSERT_FALSE(applyBoardProfileBlob("{oops", 5));
    TEST_ASSERT_EQUAL_STRING("test_board", runtimeBoardProfile().board_id);
}

void test_applyEmptyBlob_keepsDefault() {
    resetRuntimeBoardProfile();
    TEST_ASSERT_FALSE(applyBoardProfileBlob("", 0));
    TEST_ASSERT_EQUAL_STRING("crowpanel_28", runtimeBoardProfile().board_id);
}

void test_applyInvalidLaterField_fromDefault_keepsDefault() {
    resetRuntimeBoardProfile();
    char blob[1200];
    buildBlob(blob, sizeof blob, "rejected_board", "st7789", 999);
    TEST_ASSERT_FALSE(applyBoardProfileBlob(blob, strlen(blob)));
    TEST_ASSERT_EQUAL_STRING("crowpanel_28", runtimeBoardProfile().board_id);
    TEST_ASSERT_EQUAL_STRING("Elecrow CrowPanel 2.8\" ESP32", runtimeBoardProfile().board_name);
    TEST_ASSERT_EQUAL(static_cast<int>(ChipFamily::Esp32),
                      static_cast<int>(runtimeBoardProfile().chip_family));
}

void test_applyInvalidLaterField_afterValid_keepsPriorIdentifiers() {
    char blob[1200];
    buildBlob(blob, sizeof blob, "board_a", "st7789", 15);
    TEST_ASSERT_TRUE(applyBoardProfileBlob(blob, strlen(blob)));

    buildBlob(blob, sizeof blob, "board_b", "st7789", 999);
    TEST_ASSERT_FALSE(applyBoardProfileBlob(blob, strlen(blob)));

    TEST_ASSERT_EQUAL_STRING("board_a", runtimeBoardProfile().board_id);
    TEST_ASSERT_EQUAL_STRING("Test Board", runtimeBoardProfile().board_name);
}

void test_catalogHoldsEveryBoardHeader() {
    TEST_ASSERT_EQUAL_UINT32(5, static_cast<uint32_t>(kCatalogCount));
    for (const BoardProfile *profile : kCatalog) {
        TEST_ASSERT_NOT_NULL(profile);
        TEST_ASSERT_TRUE(profile->board_id[0] != '\0');
    }
}

void test_catalogLookupIsScopedToTheChipFamily() {
    TEST_ASSERT_NOT_NULL(catalogBoard("crowpanel_28", ChipFamily::Esp32));
    TEST_ASSERT_NOT_NULL(catalogBoard("waveshare_s3_28", ChipFamily::Esp32s3));
    TEST_ASSERT_NULL(catalogBoard("waveshare_s3_28", ChipFamily::Esp32));
    TEST_ASSERT_NULL(catalogBoard("crowpanel_28", ChipFamily::Esp32s3));
    TEST_ASSERT_NULL(catalogBoard("nope", ChipFamily::Esp32));
    TEST_ASSERT_NULL(catalogBoard("", ChipFamily::Esp32));
    TEST_ASSERT_NULL(catalogBoard(nullptr, ChipFamily::Esp32));
}

void test_applyCatalogBoard_switchesTheRuntimeProfile() {
    resetRuntimeBoardProfile();
    TEST_ASSERT_TRUE(applyCatalogBoard("generic_ili9341_gt911"));
    TEST_ASSERT_EQUAL_STRING("generic_ili9341_gt911", runtimeBoardProfile().board_id);
    TEST_ASSERT_EQUAL(static_cast<int>(TouchDriver::GT911),
                      static_cast<int>(runtimeBoardProfile().touch.driver));
}

void test_applyCatalogBoard_refusesAnotherChipFamily() {
    resetRuntimeBoardProfile();
    TEST_ASSERT_FALSE(applyCatalogBoard("waveshare_s3_28"));
    TEST_ASSERT_EQUAL_STRING("crowpanel_28", runtimeBoardProfile().board_id);
}

void test_applyCatalogBoard_refusesAnUnknownIdAndKeepsTheProfile() {
    resetRuntimeBoardProfile();
    TEST_ASSERT_TRUE(applyCatalogBoard("generic_ili9341"));
    TEST_ASSERT_FALSE(applyCatalogBoard("not_a_board"));
    TEST_ASSERT_EQUAL_STRING("generic_ili9341", runtimeBoardProfile().board_id);
}

int main(int /*argc*/, char ** /*argv*/) {
    UNITY_BEGIN();
    RUN_TEST(test_validBlob_parsesAllFields);
    RUN_TEST(test_invalidJson_rejected);
    RUN_TEST(test_nonObjectRoot_rejected);
    RUN_TEST(test_wrongMagic_rejected);
    RUN_TEST(test_newerFormatVersion_rejected);
    RUN_TEST(test_missingProfileFields_rejected);
    RUN_TEST(test_outOfRangePin_rejected);
    RUN_TEST(test_cst3530TouchSlug_parses);
    RUN_TEST(test_unknownEnum_rejected);
    RUN_TEST(test_runtimeDefaultsToCompileTimeBoard);
    RUN_TEST(test_applyValidBlob_overridesRuntimeProfile);
    RUN_TEST(test_applyInvalidBlob_keepsPriorProfile);
    RUN_TEST(test_applyEmptyBlob_keepsDefault);
    RUN_TEST(test_applyInvalidLaterField_fromDefault_keepsDefault);
    RUN_TEST(test_applyInvalidLaterField_afterValid_keepsPriorIdentifiers);
    RUN_TEST(test_catalogHoldsEveryBoardHeader);
    RUN_TEST(test_catalogLookupIsScopedToTheChipFamily);
    RUN_TEST(test_applyCatalogBoard_switchesTheRuntimeProfile);
    RUN_TEST(test_applyCatalogBoard_refusesAnotherChipFamily);
    RUN_TEST(test_applyCatalogBoard_refusesAnUnknownIdAndKeepsTheProfile);
    return UNITY_END();
}
