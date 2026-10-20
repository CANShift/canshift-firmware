#pragma once

#include <stdint.h>

namespace UnitDisplay {

const char *symbolFor(const char *declaredUnit);

float valueFor(float value, const char *declaredUnit);

uint8_t decimalsFor(uint8_t decimals, const char *declaredUnit);

const char *convertibleUnitFor(const char *signalId, const char *configSuffix);

const char *displayUnitFor(const char *signalId, const char *configSuffix);

}
