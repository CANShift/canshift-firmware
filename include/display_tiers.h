#pragma once

#include <stddef.h>
#include <stdint.h>

namespace canshift::display {

struct FaceLadder {
    const uint8_t *sizes;
    size_t count;
};

struct DisplayTier {
    const char *id;
    uint16_t designWidth;
    uint16_t designHeight;
    uint8_t columns;
    uint8_t rows;
    uint8_t maxWidgetsPerPage;
    FaceLadder valueFaces;
    FaceLadder labelFaces;
};

namespace tiers {

inline constexpr uint8_t kBaseValueFaces[] = {13, 15, 17, 22, 32, 44, 48, 64, 84};
inline constexpr uint8_t kBaseLabelFaces[] = {10, 12, 13, 14, 15, 16};
inline constexpr uint8_t kMediumValueFaces[] = {17, 22, 28, 36, 48, 64, 72, 96, 120};
inline constexpr uint8_t kMediumLabelFaces[] = {13, 15, 17, 19, 21, 23};
inline constexpr uint8_t kLargeValueFaces[] = {22, 28, 36, 48, 64, 88, 104, 128, 168};
inline constexpr uint8_t kLargeLabelFaces[] = {17, 20, 23, 26, 29, 32};

template <size_t N>
constexpr FaceLadder ladderOf(const uint8_t (&sizes)[N]) {
    return {sizes, N};
}

} // namespace tiers

inline constexpr DisplayTier kDisplayTiers[] = {
    {"base", 320, 240, 12, 12, 12, tiers::ladderOf(tiers::kBaseValueFaces),
     tiers::ladderOf(tiers::kBaseLabelFaces)},
    {"medium", 480, 320, 16, 14, 18, tiers::ladderOf(tiers::kMediumValueFaces),
     tiers::ladderOf(tiers::kMediumLabelFaces)},
    {"large", 800, 480, 24, 20, 28, tiers::ladderOf(tiers::kLargeValueFaces),
     tiers::ladderOf(tiers::kLargeLabelFaces)},
};

inline constexpr size_t kDisplayTierCount = sizeof(kDisplayTiers) / sizeof(kDisplayTiers[0]);

inline constexpr const DisplayTier &kBaseTier = kDisplayTiers[0];

constexpr bool tierFitsPanel(const DisplayTier &tier, uint16_t width, uint16_t height) {
    return tier.designWidth <= width && tier.designHeight <= height;
}

constexpr uint32_t tierDesignArea(const DisplayTier &tier) {
    return static_cast<uint32_t>(tier.designWidth) * static_cast<uint32_t>(tier.designHeight);
}

inline const DisplayTier &tierForPanel(uint16_t width, uint16_t height) {
    const DisplayTier *best = &kBaseTier;
    for (const DisplayTier &tier : kDisplayTiers) {
        if (tierFitsPanel(tier, width, height) && tierDesignArea(tier) > tierDesignArea(*best))
            best = &tier;
    }
    return *best;
}

} // namespace canshift::display
