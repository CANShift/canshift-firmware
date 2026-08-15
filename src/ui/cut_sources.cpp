#include "ui/cut_sources.h"

#include "runtime/signal_store.h"
#include "util/format_float.h"
#include "util/text_join.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr SignalId kNoSignal = SignalIds::SIGNAL_COUNT;
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

} // namespace

const CutSources::Source CutSources::kSources[CutSources::kSourceCount] = {
    {SignalIds::FLAG_BOOST_CUT,
     {"OVERBOOST", SignalIds::BOOST_BAR, 2, nullptr},
     {"LIMIT", SignalIds::BOOST_TARGET_BAR, 2, nullptr}},
    {SignalIds::FLAG_FUEL_CUT,
     {"OIL PRESS", SignalIds::OIL_PRESS_BAR, 1, nullptr},
     {"ENGINE PROTECT", kNoSignal, 0, nullptr}},
    {SignalIds::FLAG_IGNITION_CUT, {"RPM", SignalIds::RPM, 0, nullptr}, kNoClause},
    {SignalIds::FLAG_IGNITION_RETARD,
     {"KNOCK CYL", SignalIds::KNOCK_CYL, 0, nullptr},
     {nullptr, SignalIds::KNOCK_COUNT, 0, "EVENTS"}},
    {SignalIds::FLAG_REV_LIMIT, {"RPM", SignalIds::RPM, 0, nullptr}, kNoClause},
    {SignalIds::FLAG_TRACTION_CUT, kNoClause, kNoClause},
    {SignalIds::FLAG_PIT_LIMIT_CUT, {"SPEED", SignalIds::SPEED_KPH, 0, nullptr}, kNoClause},
    {SignalIds::FLAG_OVERHEAT_PROTECT, {"WATER", SignalIds::COOLANT_TEMP_C, 0, nullptr}, kNoClause},
    {SignalIds::FLAG_LIMP_MODE,
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
