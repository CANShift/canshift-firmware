#include "workshop_mode_rs.h"

#include <stdint.h>
#include <string.h>
#include <unity.h>

namespace {

constexpr uint32_t kSixteenMbFilesystem = 0x760000;
constexpr uint32_t kFourMbFilesystem = 0xD0000;

void fillCounting(uint8_t *entropy, size_t len) {
    for (size_t i = 0; i < len; ++i)
        entropy[i] = static_cast<uint8_t>(i);
}

void test_entryWalksDrivingEnteringActive() {
    const uint8_t entering =
        workshop_next_state_rs(kWorkshopStateDriving, kWorkshopEventRequest, true);
    TEST_ASSERT_EQUAL_UINT8(kWorkshopStateEntering, entering);
    TEST_ASSERT_EQUAL_UINT8(kWorkshopStateActive,
                            workshop_next_state_rs(entering, kWorkshopEventEntered, false));
}

void test_requestIsRefusedWhenNotOffered() {
    TEST_ASSERT_EQUAL_UINT8(
        kWorkshopStateDriving,
        workshop_next_state_rs(kWorkshopStateDriving, kWorkshopEventRequest, false));
}

void test_failedEntryReturnsToDriving() {
    TEST_ASSERT_EQUAL_UINT8(
        kWorkshopStateDriving,
        workshop_next_state_rs(kWorkshopStateEntering, kWorkshopEventFailed, false));
}

void test_resumeEntersWithoutTheStationaryGate() {
    TEST_ASSERT_EQUAL_UINT8(
        kWorkshopStateEntering,
        workshop_next_state_rs(kWorkshopStateDriving, kWorkshopEventResume, false));
    TEST_ASSERT_EQUAL_UINT8(
        kWorkshopStateActive,
        workshop_next_state_rs(kWorkshopStateActive, kWorkshopEventResume, true));
}

bool resumes(uint8_t reason, uint32_t filesystemBytes, bool imagePendingVerify) {
    return workshop_resume_after_reboot_rs(reason, filesystemBytes, imagePendingVerify);
}

void test_updateResumesOnlyOnItsFirstBootOnASupportedBoard() {
    TEST_ASSERT_TRUE(resumes(kWorkshopResumeUpdate, kSixteenMbFilesystem, true));
    TEST_ASSERT_FALSE(resumes(kWorkshopResumeNone, kSixteenMbFilesystem, true));
    TEST_ASSERT_FALSE(resumes(kWorkshopResumeUpdate, kSixteenMbFilesystem, false));
    TEST_ASSERT_FALSE(resumes(kWorkshopResumeUpdate, kFourMbFilesystem, true));
}

void test_configRestartResumesWithoutAPendingImage() {
    TEST_ASSERT_TRUE(resumes(kWorkshopResumeConfig, kSixteenMbFilesystem, false));
    TEST_ASSERT_TRUE(resumes(kWorkshopResumeConfig, kSixteenMbFilesystem, true));
    TEST_ASSERT_FALSE(resumes(kWorkshopResumeConfig, kFourMbFilesystem, false));
}

void test_anUnknownReasonNeverResumes() {
    TEST_ASSERT_FALSE(resumes(9, kSixteenMbFilesystem, true));
}

void test_onlyTheSixteenMegabyteLayoutIsSupported() {
    TEST_ASSERT_TRUE(workshop_board_supported_rs(kSixteenMbFilesystem));
    TEST_ASSERT_FALSE(workshop_board_supported_rs(kFourMbFilesystem));
}

void test_offeredWhileStoppedOrUnmapped() {
    TEST_ASSERT_TRUE(workshop_offered_rs(kSixteenMbFilesystem, true, true, 0.0f));
    TEST_ASSERT_TRUE(workshop_offered_rs(kSixteenMbFilesystem, false, false, 0.0f));
    TEST_ASSERT_FALSE(workshop_offered_rs(kSixteenMbFilesystem, true, true, 12.0f));
    TEST_ASSERT_FALSE(workshop_offered_rs(kFourMbFilesystem, true, true, 0.0f));
}

void test_generatedCredentialsAreValidAndJoinable() {
    uint8_t entropy[kWorkshopEntropyBytes];
    fillCounting(entropy, sizeof(entropy));
    char ssid[kWorkshopSsidCap];
    char password[kWorkshopPasswordCap];
    TEST_ASSERT_TRUE(workshop_generate_credentials_rs(entropy, sizeof(entropy), ssid, sizeof(ssid),
                                                      password, sizeof(password)));
    TEST_ASSERT_EQUAL_STRING("CANShift-ABCD", ssid);
    TEST_ASSERT_TRUE(workshop_credentials_valid_rs(ssid, password));

    char payload[kWorkshopJoinPayloadCap];
    const size_t len = workshop_join_payload_rs(ssid, password, payload, sizeof(payload));
    TEST_ASSERT_EQUAL_STRING("WIFI:T:WPA;S:CANShift-ABCD;P:efghjkmnpqrs;;", payload);
    TEST_ASSERT_EQUAL_size_t(strlen(payload), len);
}

void test_storedGarbageIsRejected() {
    TEST_ASSERT_FALSE(workshop_credentials_valid_rs("", "abcdefgh"));
    TEST_ASSERT_FALSE(workshop_credentials_valid_rs("CANShift-ABCD", "short"));
}

WorkshopMotionRs stillWatch() {
    WorkshopMotionRs watch = {};
    return watch;
}

bool driveFor(WorkshopMotionRs &watch, float speedKph, uint32_t fromMs, uint32_t untilMs) {
    bool due = false;
    for (uint32_t nowMs = fromMs; nowMs <= untilMs; nowMs += 100)
        due = workshop_motion_exit_due_rs(&watch, true, speedKph, nowMs);
    return due;
}

void test_sustainedMotionLeavesWorkshopMode() {
    WorkshopMotionRs watch = stillWatch();
    TEST_ASSERT_FALSE(driveFor(watch, 20.0f, 1000, 1000 + kWorkshopMotionExitMs - 100));
    TEST_ASSERT_TRUE(
        workshop_motion_exit_due_rs(&watch, true, 20.0f, 1000 + kWorkshopMotionExitMs));
}

void test_aBriefRollDoesNotLeave() {
    WorkshopMotionRs watch = stillWatch();
    TEST_ASSERT_FALSE(driveFor(watch, 20.0f, 1000, 2000));
    TEST_ASSERT_FALSE(workshop_motion_exit_due_rs(&watch, true, 0.0f, 2100));
    TEST_ASSERT_FALSE(driveFor(watch, 20.0f, 2200, 2200 + kWorkshopMotionExitMs - 100));
}

void test_crawlingBelowTheThresholdStays() {
    WorkshopMotionRs watch = stillWatch();
    TEST_ASSERT_FALSE(driveFor(watch, 4.0f, 1000, 1000 + 4 * kWorkshopMotionExitMs));
}

void test_aStaleSpeedNeverLeaves() {
    WorkshopMotionRs watch = stillWatch();
    for (uint32_t nowMs = 1000; nowMs <= 1000 + 4 * kWorkshopMotionExitMs; nowMs += 100)
        TEST_ASSERT_FALSE(workshop_motion_exit_due_rs(&watch, false, 80.0f, nowMs));
}

void test_aNullWatchNeverLeaves() {
    TEST_ASSERT_FALSE(workshop_motion_exit_due_rs(nullptr, true, 80.0f, 1000));
}

void test_socketOriginAllowsTheDashAndNonBrowserClientsOnly() {
    const char *apIp = "192.168.4.1";
    const char *host = "canshift";
    TEST_ASSERT_TRUE(workshop_origin_allowed_rs(nullptr, apIp, host));
    TEST_ASSERT_TRUE(workshop_origin_allowed_rs("http://192.168.4.1", apIp, host));
    TEST_ASSERT_TRUE(workshop_origin_allowed_rs("http://canshift.local", apIp, host));
    TEST_ASSERT_FALSE(workshop_origin_allowed_rs("http://evil.example", apIp, host));
    TEST_ASSERT_FALSE(workshop_origin_allowed_rs("http://localhost:5173", apIp, host));
    TEST_ASSERT_FALSE(workshop_origin_allowed_rs("https://192.168.4.1", apIp, host));
    TEST_ASSERT_FALSE(workshop_origin_allowed_rs("null", apIp, host));
    TEST_ASSERT_FALSE(workshop_origin_allowed_rs("", apIp, host));
}

constexpr uint32_t kSeenMs = 10000;

uint8_t admit(bool hasClient, bool sameClient, uint32_t nowMs) {
    return workshop_socket_admission_rs(hasClient, sameClient, kSeenMs, nowMs);
}

void test_aFirstTunerIsAdopted() {
    TEST_ASSERT_EQUAL_UINT8(kWorkshopAdmitAdopt, admit(false, false, kSeenMs));
}

void test_aSecondTunerIsRefusedWhileTheFirstIsActive() {
    TEST_ASSERT_EQUAL_UINT8(kWorkshopAdmitRefuseBusy,
                            admit(true, false, kSeenMs + kWorkshopSocketBusyWindowMs - 1));
}

void test_aStaleTunerIsReplaced() {
    TEST_ASSERT_EQUAL_UINT8(kWorkshopAdmitReplaceStale,
                            admit(true, false, kSeenMs + kWorkshopSocketBusyWindowMs));
}

void test_theSameSocketIsReadopted() {
    TEST_ASSERT_EQUAL_UINT8(kWorkshopAdmitAdopt, admit(true, true, kSeenMs + 1));
}

void test_theBusyWindowSurvivesAMillisWrap() {
    TEST_ASSERT_EQUAL_UINT8(kWorkshopAdmitRefuseBusy,
                            workshop_socket_admission_rs(true, false, UINT32_MAX - 100, 100));
}

} // namespace

void setUp() {}

void tearDown() {}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_entryWalksDrivingEnteringActive);
    RUN_TEST(test_requestIsRefusedWhenNotOffered);
    RUN_TEST(test_failedEntryReturnsToDriving);
    RUN_TEST(test_resumeEntersWithoutTheStationaryGate);
    RUN_TEST(test_updateResumesOnlyOnItsFirstBootOnASupportedBoard);
    RUN_TEST(test_configRestartResumesWithoutAPendingImage);
    RUN_TEST(test_anUnknownReasonNeverResumes);
    RUN_TEST(test_onlyTheSixteenMegabyteLayoutIsSupported);
    RUN_TEST(test_offeredWhileStoppedOrUnmapped);
    RUN_TEST(test_generatedCredentialsAreValidAndJoinable);
    RUN_TEST(test_storedGarbageIsRejected);
    RUN_TEST(test_sustainedMotionLeavesWorkshopMode);
    RUN_TEST(test_aBriefRollDoesNotLeave);
    RUN_TEST(test_crawlingBelowTheThresholdStays);
    RUN_TEST(test_aStaleSpeedNeverLeaves);
    RUN_TEST(test_aNullWatchNeverLeaves);
    RUN_TEST(test_socketOriginAllowsTheDashAndNonBrowserClientsOnly);
    RUN_TEST(test_aFirstTunerIsAdopted);
    RUN_TEST(test_aSecondTunerIsRefusedWhileTheFirstIsActive);
    RUN_TEST(test_aStaleTunerIsReplaced);
    RUN_TEST(test_theSameSocketIsReadopted);
    RUN_TEST(test_theBusyWindowSurvivesAMillisWrap);
    return UNITY_END();
}
