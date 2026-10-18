#include "ui/cut_sources.h"

#include "runtime/signal_store.h"
#include "util/format_float.h"
#include "util/text_join.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr SignalId kNoSignal = SignalIds::kSignalCount;
constexpr float kFlagOnLevel = 0.5f;
constexpr size_t kClauseBufLen = 48;
constexpr size_t kValueBufLen = 16;

constexpr CutSources::Clause kNoClause = {nullptr, kNoSignal, 0, nullptr};

bool clauseIsSilent(const CutSources::Clause &clause) {
    return clause.label == nullptr && clause.signal == kNoSignal;
}

bool readClauseValue(const CutSources::Clause &clause, const SignalStore::SignalValue *snap,
                     char *out, size_t len) {
    out[0] = '\0';
    if (clause.signal == kNoSignal)
        return true;
    if (!snap[clause.signal].valid)
        return false;
    FloatFormat::formatFixed(out, len, snap[clause.signal].raw, clause.decimals);
    return true;
}

bool composeClause(const CutSources::Clause &clause, const SignalStore::SignalValue *snap,
                   char *out, size_t len) {
    out[0] = '\0';
    if (clauseIsSilent(clause))
        return false;
    char value[kValueBufLen];
    if (!readClauseValue(clause, snap, value, sizeof(value)))
        return false;

    const char *parts[] = {clause.label, value, clause.trailer};
    size_t used = 0;
    for (const char *part : parts) {
        if (part == nullptr || part[0] == '\0')
            continue;
        const int n = snprintf(out + used, len - used, "%s%s", (used > 0) ? " " : "", part);
        if (n < 0 || static_cast<size_t>(n) >= len - used)
            break;
        used += static_cast<size_t>(n);
    }
    return used > 0;
}

bool flagIsAsserted(SignalId flag, const SignalStore::SignalValue *snap) {
    return snap[flag].valid && snap[flag].raw >= kFlagOnLevel;
}

}

const CutSources::Source CutSources::kSources[CutSources::kSourceCount] = {
    {SignalIds::kFlagBoostCut,
     {"OVERBOOST", SignalIds::kBoostBar, 2, nullptr},
     {"LIMIT", SignalIds::kBoostTargetBar, 2, nullptr}},
    {SignalIds::kFlagFuelCut,
     {"OIL PRESS", SignalIds::kOilPressBar, 1, nullptr},
     {"ENGINE PROTECT", kNoSignal, 0, nullptr}},
    {SignalIds::kFlagIgnitionCut, {"RPM", SignalIds::kRpm, 0, nullptr}, kNoClause},
    {SignalIds::kFlagIgnitionRetard,
     {"KNOCK CYL", SignalIds::kKnockCyl, 0, nullptr},
     {nullptr, SignalIds::kKnockCount, 0, "EVENTS"}},
    {SignalIds::kFlagRevLimit, {"RPM", SignalIds::kRpm, 0, nullptr}, kNoClause},
    {SignalIds::kFlagTractionCut, kNoClause, kNoClause},
    {SignalIds::kFlagPitLimitCut, {"SPEED", SignalIds::kSpeedKph, 0, nullptr}, kNoClause},
    {SignalIds::kFlagOverheatProtect, {"WATER", SignalIds::kCoolantTempC, 0, nullptr}, kNoClause},
    {SignalIds::kFlagLimpMode,
     {"ECU FAULT", kNoSignal, 0, nullptr},
     {"ENGINE PROTECT", kNoSignal, 0, nullptr}},
};

uint16_t CutSources::activeFlags(const SignalStore::SignalValue *snap) {
    uint16_t flags = 0;
    if (snap == nullptr)
        return 0;
    for (size_t kind = 0; kind < kSourceCount; ++kind) {
        if (flagIsAsserted(kSources[kind].flag, snap))
            flags |= static_cast<uint16_t>(1u << kind);
    }
    return flags;
}

SignalId CutSources::offendingSignal(uint8_t kind) {
    if (kind >= kSourceCount)
        return kNoSignal;
    return kSources[kind].primary.signal;
}

void CutSources::composeDetail(uint8_t kind, const SignalStore::SignalValue *snap, char *out,
                               size_t len) {
    out[0] = '\0';
    if (kind >= kSourceCount || len == 0 || snap == nullptr)
        return;

    char primary[kClauseBufLen];
    char secondary[kClauseBufLen];
    const char *clauses[] = {
        composeClause(kSources[kind].primary, snap, primary, sizeof(primary)) ? primary : nullptr,
        composeClause(kSources[kind].secondary, snap, secondary, sizeof(secondary)) ? secondary
                                                                                    : nullptr,
    };

    size_t used = 0;
    for (const char *clause : clauses) {
        if (clause == nullptr)
            continue;
        if (!TextJoin::append(out, len, used, clause))
            return;
    }
}
