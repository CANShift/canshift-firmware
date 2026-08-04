#ifdef SPIKE_ALL_DRIVERS

    #define LGFX_USE_V1
    #include <LovyanGFX.hpp>

namespace {

lgfx::Panel_ILI9341 s_panelIli9341;
lgfx::Panel_ST7789 s_panelSt7789;
lgfx::Touch_XPT2046 s_touchXpt2046;
lgfx::Touch_GT911 s_touchGt911;
lgfx::Touch_CST816S s_touchCst816s;

} // namespace

volatile uintptr_t g_spikeDriverSink;

extern "C" void __attribute__((constructor)) spikeReferenceAllDrivers(void) {
    lgfx::Panel_Device *const panels[] = {&s_panelIli9341, &s_panelSt7789};
    lgfx::ITouch *const touches[] = {&s_touchXpt2046, &s_touchGt911, &s_touchCst816s};
    for (auto *p : panels) {
        g_spikeDriverSink ^= reinterpret_cast<uintptr_t>(p);
    }
    for (auto *t : touches) {
        g_spikeDriverSink ^= reinterpret_cast<uintptr_t>(t);
    }
}

#endif
