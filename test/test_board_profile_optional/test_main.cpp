#include "config/board_profile_loader.h"

#include "boards/catalog.h"

#include <stdio.h>
#include <string.h>
#include <unity.h>

using namespace canshift::boards;

namespace {

constexpr char kBlobHead[] =
    "{\"magic\":\"CANSHIFT_BOARD\",\"schema\":\"board-profile\",\"formatVersion\":1,"
    "\"profile\":{\"board_id\":\"imu_board\",\"board_name\":\"IMU Board\",\"chip_family\":"
    "\"esp32s3\",\"lcd\":{\"driver\":\"st7789\",\"pin_mosi\":45,\"pin_miso\":-1,\"pin_sclk\":40,"
    "\"pin_cs\":42,\"pin_dc\":41,\"pin_rst\":39,\"pin_bl\":5,\"freq_write_hz\":80000000,"
    "\"panel_width\":240,\"panel_height\":320,\"memory_width\":240,\"memory_height\":320,"
    "\"default_rotation\":1,\"rgb_order_bgr\":false,\"invert\":true,\"bus_shared_with_touch\":"
    "false,\"readable\":false,\"color_depth\":16},\"backlight\":{\"present\":true,"
    "\"pwm_channel\":1,\"pwm_freq_hz\":20000,\"default_duty\":200,\"invert\":false},"
    "\"touch\":{\"driver\":\"cst3530\",\"pin_cs\":-1,\"pin_irq\":-1,\"freq_hz\":400000,"
    "\"needs_calibration\":false,\"pin_sda\":1,\"pin_scl\":3%s},\"can\":{\"controller\":"
    "\"esp_twai\",\"pin_tx\":17,\"pin_rx\":18,\"default_speed_kbps\":500},\"storage\":"
    "{\"spiffs_present\":true,\"spiffs_size_kb\":1024,\"sd_present\":true,\"sd_pin_cs\":-1%s},"
    "\"conn\":{\"wifi_supported\":true,\"ble_supported\":true,\"psram_present\":true}%s}}";

constexpr char kImuSection[] =
    ",\"imu\":{\"driver\":\"qmi8658\",\"pin_sda\":11,\"pin_scl\":10,\"i2c_addr\":107,"
    "\"freq_hz\":400000,\"screen_x\":\"-y\",\"screen_y\":\"x\",\"screen_z\":\"z\"}";

constexpr char kAudioSection[] =
    ",\"audio\":{\"present\":true,\"pin_bck\":48,\"pin_ws\":38,\"pin_dout\":47}";

char s_blob[1600];
char s_idBuf[kBoardIdCapacity];
char s_nameBuf[kBoardNameCapacity];

BoardProfileParse parseWith(const char *touchTail, const char *storageTail,
                            const char *extraSections, BoardProfile &out) {
    snprintf(s_blob, sizeof s_blob, kBlobHead, touchTail, storageTail, extraSections);
    return parseBoardProfileBlob(s_blob, strlen(s_blob), out, s_idBuf, sizeof s_idBuf, s_nameBuf,
                                 sizeof s_nameBuf);
}

int asInt(BoardProfileParse parse) {
    return static_cast<int>(parse);
}

}

void setUp() {}
void tearDown() {}

void test_blobWithoutImu_hasNoImu() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::Ok), asInt(parseWith("", "", "", out)));
    TEST_ASSERT_EQUAL(static_cast<int>(ImuDriver::None), static_cast<int>(out.imu.driver));
    TEST_ASSERT_EQUAL_INT(-1, out.imu.pin_sda);
}

void test_blobWithoutTouchReset_defaultsTheResetPinOff() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::Ok), asInt(parseWith("", "", "", out)));
    TEST_ASSERT_EQUAL_INT(-1, out.touch.pin_rst);
}

void test_blobWithTouchReset_readsTheResetPin() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::Ok),
                      asInt(parseWith(",\"pin_rst\":2", "", "", out)));
    TEST_ASSERT_EQUAL_INT(2, out.touch.pin_rst);
}

void test_blobWithImu_parsesEveryField() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::Ok), asInt(parseWith("", "", kImuSection, out)));
    TEST_ASSERT_EQUAL(static_cast<int>(ImuDriver::QMI8658), static_cast<int>(out.imu.driver));
    TEST_ASSERT_EQUAL_INT(11, out.imu.pin_sda);
    TEST_ASSERT_EQUAL_INT(10, out.imu.pin_scl);
    TEST_ASSERT_EQUAL_UINT8(0x6B, out.imu.i2c_addr);
    TEST_ASSERT_EQUAL_UINT32(400000u, out.imu.freq_hz);
    TEST_ASSERT_EQUAL(static_cast<int>(ImuAxis::NegY), static_cast<int>(out.imu.screen_x));
    TEST_ASSERT_EQUAL(static_cast<int>(ImuAxis::PosX), static_cast<int>(out.imu.screen_y));
    TEST_ASSERT_EQUAL(static_cast<int>(ImuAxis::PosZ), static_cast<int>(out.imu.screen_z));
}

void test_imuNamingOneAxisTwice_rejected() {
    BoardProfile out{};
    const char *twice = ",\"imu\":{\"driver\":\"qmi8658\",\"pin_sda\":11,\"pin_scl\":10,"
                        "\"i2c_addr\":107,\"freq_hz\":400000,\"screen_x\":\"x\",\"screen_y\":"
                        "\"-x\",\"screen_z\":\"z\"}";
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::WrongShape), asInt(parseWith("", "", twice, out)));
}

void test_imuWithUnknownDriver_rejected() {
    BoardProfile out{};
    const char *unknown = ",\"imu\":{\"driver\":\"mpu6050\",\"pin_sda\":11,\"pin_scl\":10,"
                          "\"i2c_addr\":104,\"freq_hz\":400000,\"screen_x\":\"x\",\"screen_y\":"
                          "\"y\",\"screen_z\":\"z\"}";
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::WrongShape), asInt(parseWith("", "", unknown, out)));
}

void test_imuThatIsNotAnObject_rejected() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::WrongShape),
                      asInt(parseWith("", "", ",\"imu\":true", out)));
}

void test_catalogDeclaresTheImuOnlyOnTheWaveshare() {
    for (const BoardProfile *profile : kCatalog) {
        const bool waveshare = strcmp(profile->board_id, "waveshare_s3_28") == 0;
        TEST_ASSERT_EQUAL(waveshare, profile->imu.driver == ImuDriver::QMI8658);
    }
    const BoardProfile *waveshare = catalogBoard("waveshare_s3_28", ChipFamily::Esp32s3);
    TEST_ASSERT_NOT_NULL(waveshare);
    TEST_ASSERT_EQUAL_UINT8(0x6B, waveshare->imu.i2c_addr);
    TEST_ASSERT_NOT_EQUAL(waveshare->touch.pin_sda, waveshare->imu.pin_sda);
}

void test_blobWithoutSdSlot_hasNoSdBus() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::Ok), asInt(parseWith("", "", "", out)));
    TEST_ASSERT_EQUAL(static_cast<int>(SdBus::None), static_cast<int>(out.storage.sd_bus));
    TEST_ASSERT_EQUAL_INT(-1, out.storage.sd_pin_clk);
    TEST_ASSERT_EQUAL_INT(-1, out.storage.sd_pin_d3);
}

void test_blobWithSdmmcSlot_parsesEveryPin() {
    BoardProfile out{};
    const char *slot = ",\"sd_bus\":\"sdmmc_1bit\",\"sd_pin_clk\":14,\"sd_pin_cmd\":17,"
                       "\"sd_pin_d0\":16,\"sd_pin_d3\":21";
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::Ok), asInt(parseWith("", slot, "", out)));
    TEST_ASSERT_EQUAL(static_cast<int>(SdBus::Sdmmc1Bit), static_cast<int>(out.storage.sd_bus));
    TEST_ASSERT_EQUAL_INT(14, out.storage.sd_pin_clk);
    TEST_ASSERT_EQUAL_INT(17, out.storage.sd_pin_cmd);
    TEST_ASSERT_EQUAL_INT(16, out.storage.sd_pin_d0);
    TEST_ASSERT_EQUAL_INT(21, out.storage.sd_pin_d3);
}

void test_sdBusWithUnknownSlug_rejected() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::WrongShape),
                      asInt(parseWith("", ",\"sd_bus\":\"sdmmc_4bit\"", "", out)));
}

void test_catalogDeclaresTheSdSlotOnlyOnTheWaveshare() {
    for (const BoardProfile *profile : kCatalog) {
        const bool waveshare = strcmp(profile->board_id, "waveshare_s3_28") == 0;
        TEST_ASSERT_EQUAL(waveshare, profile->storage.sd_bus == SdBus::Sdmmc1Bit);
    }
}

void test_blobWithoutAudio_staysSilent() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::Ok), asInt(parseWith("", "", "", out)));
    TEST_ASSERT_FALSE(out.audio.present);
    TEST_ASSERT_EQUAL_INT(-1, out.audio.pin_bck);
    TEST_ASSERT_EQUAL_INT(-1, out.audio.pin_dout);
}

void test_blobWithAudio_parsesEveryPin() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::Ok), asInt(parseWith("", "", kAudioSection, out)));
    TEST_ASSERT_TRUE(out.audio.present);
    TEST_ASSERT_EQUAL_INT(48, out.audio.pin_bck);
    TEST_ASSERT_EQUAL_INT(38, out.audio.pin_ws);
    TEST_ASSERT_EQUAL_INT(47, out.audio.pin_dout);
}

void test_blobWithImuAndAudio_parsesBoth() {
    BoardProfile out{};
    char sections[400];
    snprintf(sections, sizeof sections, "%s%s", kImuSection, kAudioSection);
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::Ok), asInt(parseWith("", "", sections, out)));
    TEST_ASSERT_EQUAL(static_cast<int>(ImuDriver::QMI8658), static_cast<int>(out.imu.driver));
    TEST_ASSERT_TRUE(out.audio.present);
}

void test_audioMarkedAbsent_parsesSilent() {
    BoardProfile out{};
    const char *absent =
        ",\"audio\":{\"present\":false,\"pin_bck\":-1,\"pin_ws\":-1,\"pin_dout\":-1}";
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::Ok), asInt(parseWith("", "", absent, out)));
    TEST_ASSERT_FALSE(out.audio.present);
}

void test_audioPresentWithAnUnwiredPin_rejected() {
    BoardProfile out{};
    const char *unwired =
        ",\"audio\":{\"present\":true,\"pin_bck\":48,\"pin_ws\":-1,\"pin_dout\":47}";
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::WrongShape), asInt(parseWith("", "", unwired, out)));
}

void test_audioMissingAPin_rejected() {
    BoardProfile out{};
    const char *missing = ",\"audio\":{\"present\":true,\"pin_bck\":48,\"pin_ws\":38}";
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::WrongShape), asInt(parseWith("", "", missing, out)));
}

void test_audioThatIsNotAnObject_rejected() {
    BoardProfile out{};
    TEST_ASSERT_EQUAL(asInt(BoardProfileParse::WrongShape),
                      asInt(parseWith("", "", ",\"audio\":true", out)));
}

void test_catalogDeclaresAudioOnlyOnTheWaveshare() {
    for (const BoardProfile *profile : kCatalog) {
        const bool waveshare = strcmp(profile->board_id, "waveshare_s3_28") == 0;
        TEST_ASSERT_EQUAL(waveshare, profile->audio.present);
    }
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_blobWithoutImu_hasNoImu);
    RUN_TEST(test_blobWithoutTouchReset_defaultsTheResetPinOff);
    RUN_TEST(test_blobWithTouchReset_readsTheResetPin);
    RUN_TEST(test_blobWithImu_parsesEveryField);
    RUN_TEST(test_imuNamingOneAxisTwice_rejected);
    RUN_TEST(test_imuWithUnknownDriver_rejected);
    RUN_TEST(test_imuThatIsNotAnObject_rejected);
    RUN_TEST(test_catalogDeclaresTheImuOnlyOnTheWaveshare);
    RUN_TEST(test_blobWithoutSdSlot_hasNoSdBus);
    RUN_TEST(test_blobWithSdmmcSlot_parsesEveryPin);
    RUN_TEST(test_sdBusWithUnknownSlug_rejected);
    RUN_TEST(test_catalogDeclaresTheSdSlotOnlyOnTheWaveshare);
    RUN_TEST(test_blobWithoutAudio_staysSilent);
    RUN_TEST(test_blobWithAudio_parsesEveryPin);
    RUN_TEST(test_blobWithImuAndAudio_parsesBoth);
    RUN_TEST(test_audioMarkedAbsent_parsesSilent);
    RUN_TEST(test_audioPresentWithAnUnwiredPin_rejected);
    RUN_TEST(test_audioMissingAPin_rejected);
    RUN_TEST(test_audioThatIsNotAnObject_rejected);
    RUN_TEST(test_catalogDeclaresAudioOnlyOnTheWaveshare);
    return UNITY_END();
}
