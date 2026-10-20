#ifndef CANSHIFT_IMU_CORE_RS_H
#define CANSHIFT_IMU_CORE_RS_H

#include <stdbool.h>
#include <stdint.h>

inline constexpr int kImuSectorCount = 8;
inline constexpr uint16_t kImuLevelSamples = 100;

inline constexpr uint8_t kImuLevelPending = 0;
inline constexpr uint8_t kImuLevelOk = 1;
inline constexpr uint8_t kImuLevelMoving = 2;
inline constexpr uint8_t kImuLevelNotOneG = 3;
inline constexpr uint8_t kImuLevelScreenFlat = 4;

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ImuVehicleBasis {
    float right[3];
    float forward[3];
    float up[3];
} ImuVehicleBasis;

typedef struct ImuCoreState {
    ImuVehicleBasis basis;
    float peaks[kImuSectorCount];
    float lateral_g;
    float longitudinal_g;
    uint8_t axis_map[3];
    uint8_t primed;
} ImuCoreState;

typedef struct ImuLevelCapture {
    float sum[3];
    float sum_magnitude;
    float sum_magnitude_sq;
    uint16_t count;
    uint16_t _pad;
} ImuLevelCapture;

bool imu_core_init_rs(ImuCoreState *core, const uint8_t *axis_map);
void imu_core_push_rs(ImuCoreState *core, float ax, float ay, float az);
bool imu_core_level_rs(ImuCoreState *core, float gx, float gy, float gz);
void imu_core_reset_peaks_rs(ImuCoreState *core);
float imu_core_combined_g_rs(const ImuCoreState *core);
void imu_core_plot_rs(const ImuCoreState *core, float range_g, int16_t radius_px, int16_t *out_dx,
                      int16_t *out_dy);
void imu_core_envelope_vertex_rs(const ImuCoreState *core, uint8_t sector, float range_g,
                                 int16_t radius_px, int16_t *out_dx, int16_t *out_dy);
uint8_t imu_level_push_rs(ImuLevelCapture *capture, const ImuCoreState *core, float ax, float ay,
                          float az);
uint8_t imu_level_gravity_rs(const ImuLevelCapture *capture, float *out_gravity);

#ifdef __cplusplus
}

static_assert(sizeof(ImuVehicleBasis) == 36, "ImuVehicleBasis layout must match rust/imu-core");
static_assert(sizeof(ImuCoreState) == 80, "ImuCoreState layout must match rust/imu-core");
static_assert(sizeof(ImuLevelCapture) == 24, "ImuLevelCapture layout must match rust/imu-core");
#endif

#endif
