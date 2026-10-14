#include "perf_timer.h"

#include <algorithm>
#include <chrono>
#include <vector>

namespace {

constexpr uint64_t kTargetSampleNs = 2'000'000;
constexpr uint32_t kMaxIterations = 1u << 24;
constexpr uint32_t kWarmupSamples = 3;
constexpr double kMedian = 0.50;
constexpr double kLowerQuartile = 0.25;
constexpr double kUpperQuartile = 0.75;

volatile uint64_t s_sink = 0;

uint64_t nowNs() {
    using namespace std::chrono;
    return static_cast<uint64_t>(
        duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count());
}

uint64_t timeBatch(PerfTimer::Op op, uint32_t iterations, uint32_t &cursor) {
    const uint64_t start = nowNs();
    for (uint32_t i = 0; i < iterations; ++i)
        op(cursor++);
    return nowNs() - start;
}

uint32_t calibrate(PerfTimer::Op op, uint32_t &cursor) {
    uint32_t iterations = 1;
    while (iterations < kMaxIterations && timeBatch(op, iterations, cursor) < kTargetSampleNs)
        iterations *= 2;
    return iterations;
}

double quantile(const std::vector<double> &sorted, double q) {
    const double position = q * static_cast<double>(sorted.size() - 1);
    const size_t lower = static_cast<size_t>(position);
    const size_t upper = std::min(lower + 1, sorted.size() - 1);
    const double weight = position - static_cast<double>(lower);
    return sorted[lower] + (sorted[upper] - sorted[lower]) * weight;
}

} // namespace

namespace PerfTimer {

Stats measure(Op op, uint32_t samples) {
    uint32_t cursor = 0;
    const uint32_t iterations = calibrate(op, cursor);
    for (uint32_t i = 0; i < kWarmupSamples; ++i)
        timeBatch(op, iterations, cursor);
    std::vector<double> perOpNs;
    perOpNs.reserve(samples);
    for (uint32_t i = 0; i < samples; ++i)
        perOpNs.push_back(static_cast<double>(timeBatch(op, iterations, cursor)) / iterations);
    std::sort(perOpNs.begin(), perOpNs.end());
    return {quantile(perOpNs, kMedian), quantile(perOpNs, kLowerQuartile),
            quantile(perOpNs, kUpperQuartile), iterations, samples};
}

void keep(uint64_t value) {
    s_sink = s_sink + value;
}

} // namespace PerfTimer
