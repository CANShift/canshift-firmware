#include "passkey_overlay.h"
#include "ui/font_manager.h"
#include "ui/overlay_scaffold.h"

#include <stdio.h>

namespace {

lv_obj_t *s_overlay = nullptr;
constexpr uint32_t kPasskeyBackdropRgb = 0x000000;

void teardown() {
    if (s_overlay) {
        lv_obj_del(s_overlay);
        s_overlay = nullptr;
    }
}

} // namespace

void PasskeyOverlay::show(uint32_t passkey) {
    teardown();

    lv_obj_t *root = OverlayScaffold::createRoot(kPasskeyBackdropRgb);

    lv_obj_t *prompt = lv_label_create(root);
    lv_label_set_text(prompt, "PAIR WITH PHONE");
    lv_obj_set_style_text_color(prompt, lv_color_hex(0x888888), 0);
    lv_obj_set_style_text_font(prompt, FontManager::label(12), 0);
    lv_obj_align(prompt, LV_ALIGN_CENTER, 0, -48);

    char buf[16];
    const uint32_t shown = passkey % 1000000u;
    snprintf(buf, sizeof(buf), "%06u", static_cast<unsigned>(shown));

    lv_obj_t *code = lv_label_create(root);
    lv_label_set_text(code, buf);
    lv_obj_set_style_text_color(code, lv_color_hex(0xE08030), 0);
    lv_obj_set_style_text_font(code, FontManager::value(24), 0);
    lv_obj_align(code, LV_ALIGN_CENTER, 0, -8);

    lv_obj_t *hint = lv_label_create(root);
    lv_label_set_text(hint, "Enter this code in CANShift mobile");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x555555), 0);
    lv_obj_set_style_text_font(hint, FontManager::label(12), 0);
    lv_obj_align(hint, LV_ALIGN_CENTER, 0, 36);

    s_overlay = root;
    lv_refr_now(nullptr);
}

void PasskeyOverlay::hide() {
    if (!s_overlay)
        return;
    teardown();
    lv_refr_now(nullptr);
}
