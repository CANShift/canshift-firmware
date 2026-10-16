#ifndef CANSHIFT_WORKSHOP_MODE_RS_H
#define CANSHIFT_WORKSHOP_MODE_RS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

inline constexpr int kWorkshopEntropyBytes = 64;
inline constexpr int kWorkshopSsidCap = 33;
inline constexpr int kWorkshopPasswordCap = 64;
inline constexpr int kWorkshopJoinPayloadCap = 224;
inline constexpr int kWorkshopTunerPathCap = 32;
inline constexpr uint32_t kWorkshopMotionExitMs = 3000;

#ifdef __cplusplus
extern "C" {
#endif

enum { kWorkshopStateDriving = 0, kWorkshopStateEntering = 1, kWorkshopStateActive = 2 };

enum {
    kWorkshopEventRequest = 0,
    kWorkshopEventEntered = 1,
    kWorkshopEventFailed = 2,
    kWorkshopEventResume = 3
};

enum { kWorkshopResumeNone = 0, kWorkshopResumeUpdate = 1, kWorkshopResumeConfig = 2 };

typedef struct {
    uint32_t moving_since_ms;
    bool moving;
} WorkshopMotionRs;

uint8_t workshop_next_state_rs(uint8_t state, uint8_t event, bool is_offered);
bool workshop_board_supported_rs(uint32_t filesystem_bytes);
bool workshop_resume_after_reboot_rs(uint8_t reason, uint32_t filesystem_bytes,
                                     bool image_pending_verify);
bool workshop_offered_rs(uint32_t filesystem_bytes, bool speed_mapped, bool speed_valid,
                         float speed_kph);
bool workshop_generate_credentials_rs(const uint8_t *entropy, size_t entropy_len, char *ssid,
                                      size_t ssid_cap, char *password, size_t password_cap);
bool workshop_credentials_valid_rs(const char *ssid, const char *password);
size_t workshop_join_payload_rs(const char *ssid, const char *password, char *out, size_t out_cap);
size_t workshop_tuner_path_rs(const char *uri, bool gzip, char *out, size_t out_cap);
bool workshop_tuner_spa_route_rs(const char *uri);
const char *workshop_tuner_content_type_rs(const char *uri);
bool workshop_motion_exit_due_rs(WorkshopMotionRs *watch, bool speed_valid, float speed_kph,
                                 uint32_t now_ms);

#ifdef __cplusplus
}

static_assert(sizeof(WorkshopMotionRs) == 8, "WorkshopMotionRs layout must match rust MotionWatch");
#endif

#endif
