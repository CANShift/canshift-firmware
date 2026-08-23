#pragma once

namespace UnitDisplay {

const char *symbolFor(const char *declaredUnit);

float valueFor(float value, const char *declaredUnit);

const char *convertibleUnitFor(const char *signalId, const char *configSuffix);

const char *displayUnitFor(const char *signalId, const char *configSuffix);

} // namespace UnitDisplay
