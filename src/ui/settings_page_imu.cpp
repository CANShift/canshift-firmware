#include "settings_page_internal.h"

#include "imu_core_rs.h"
#include "layout_scale.h"
#include "runtime/imu_feed.h"

#include <lvgl.h>
#include <stdint.h>

namespace SettingsPageInternal {

namespace {

constexpr const char *kLevelIdle = "LEVEL G-METER";
constexpr const char *kLevelRunning = "LEVELLING - HOLD STILL";
constexpr uint32_t kLevelPollMs = 200;

struct LevelOutcome {
    uint8_t status;
    const char *text;
    bool ok;
};

constexpr LevelOutcome kLevelOutcomes[] = {
    {kImuLevelOk, "LEVELLED", true},
    {kImuLevelMoving, "CAR MOVING - TRY AGAIN", false},
    {kImuLevelNotOneG, "READING OFF - TRY AGAIN", false},
    {kImuLevelScreenFlat, "SCREEN TOO FLAT", false},
    {ImuFeed::kLevelNotSaved, "LEVELLED - NOT SAVED", false},
};

lv_obj_t *s_levelBtn = nullptr;
lv_timer_t *s_levelTimer = nullptr;

void setLevelLabel(const char *text, uint32_t rgb) {
    lv_obj_t *label = s_levelBtn ? lv_obj_get_child(s_levelBtn, 0) : nullptr;
    if (!label)
        return;
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_hex(rgb), 0);
}

const LevelOutcome *outcomeFor(uint8_t status) {
    for (const LevelOutcome &outcome : kLevelOutcomes) {
        if (outcome.status == status)
            return &outcome;
    }
    return nullptr;
}

void stopLevelTimer() {
    if (!s_levelTimer)
        return;
    lv_timer_del(s_levelTimer);
    s_levelTimer = nullptr;
}

void restoreLevelLabel(lv_timer_t *) {
    s_levelTimer = nullptr;
    setLevelLabel(kLevelIdle, kClrMuted);
}

void pollLevel(lv_timer_t *) {
    const LevelOutcome *outcome = outcomeFor(ImuFeed::levelStatus());
    if (!outcome)
        return;
    stopLevelTimer();
    setLevelLabel(outcome->text, outcome->ok ? kClrText : kClrAccent);
    s_levelTimer = lv_timer_create(restoreLevelLabel, kResetFeedbackMs, nullptr);
    lv_timer_set_repeat_count(s_levelTimer, 1);
}

void onLevelImu(lv_event_t *) {
    stopLevelTimer();
    ImuFeed::requestLevel();
    setLevelLabel(kLevelRunning, kClrAccent);
    s_levelTimer = lv_timer_create(pollLevel, kLevelPollMs, nullptr);
}

}

void buildLevelImuRow(int16_t &y, int16_t rowW) {
    s_levelBtn = makeFullButton(s_panel, kLevelIdle, kClrBtnBg, kClrBtnBdr, kClrMuted, onLevelImu);
    lv_obj_set_pos(s_levelBtn, LayoutScale::x(kPadH), y);
    lv_obj_set_size(s_levelBtn, rowW, kBtnH);
    y += kBtnH;
}

}
