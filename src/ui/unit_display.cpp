#include "ui/unit_display.h"

#include "unit_convert_rs.h"

#include "config/config_loader.h"
#include "ui/signal_presentation.h"

namespace UnitDisplay {

namespace {

uint8_t activeSystem() {
    return ConfigLoader::getDashboardConfig().units == CfgUnitSystem::IMPERIAL
               ? UNIT_SYSTEM_IMPERIAL
               : UNIT_SYSTEM_METRIC;
}

bool isRenderable(const char *declaredUnit) {
    return declaredUnit != nullptr && declaredUnit[0] != '\0';
}

const char *declaredSignalUnit(const char *signalId) {
    if (!signalId || signalId[0] == '\0')
        return "";
    const CfgSignalDef *def = ConfigLoader::findSignal(signalId);
    if (def && def->unit[0] != '\0')
        return def->unit;
    return SignalPresentation::unitForSignal(signalId);
}

} // namespace

const char *symbolFor(const char *declaredUnit) {
    if (!isRenderable(declaredUnit))
        return declaredUnit;
    const char *swapped = unit_display_symbol_rs(declaredUnit, activeSystem());
    return swapped != nullptr ? swapped : declaredUnit;
}

float valueFor(float value, const char *declaredUnit) {
    if (!isRenderable(declaredUnit))
        return value;
    return unit_display_value_rs(value, declaredUnit, activeSystem());
}

const char *convertibleUnitFor(const char *signalId, const char *configSuffix) {
    if (isRenderable(configSuffix))
        return "";
    return declaredSignalUnit(signalId);
}

const char *displayUnitFor(const char *signalId, const char *configSuffix) {
    if (isRenderable(configSuffix))
        return configSuffix;
    return symbolFor(declaredSignalUnit(signalId));
}

} // namespace UnitDisplay
