#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <cstdlib>

struct AndroidRenderMode {
    uint32_t width, height;
    uint32_t percent;
};

inline constexpr uint32_t kAllResolutionPercents[] = {
    25, 30, 35, 40, 45, 50, 55, 60, 65, 70, 75, 80, 85, 90, 95, 100, 125, 150, 200
};
inline constexpr size_t kNumAllResolutionPercents = sizeof(kAllResolutionPercents) / sizeof(kAllResolutionPercents[0]);

inline uint32_t findClosestResolutionPercentIndex(uint32_t percent) {
    uint32_t bestIdx = 0;
    int bestDiff = 999999;
    for (size_t i = 0; i < kNumAllResolutionPercents; ++i) {
        int diff = std::abs((int)kAllResolutionPercents[i] - (int)percent);
        if (diff < bestDiff) {
            bestDiff = diff;
            bestIdx = (uint32_t)i;
        }
    }
    return bestIdx;
}

inline std::array<AndroidRenderMode, 6> androidRenderModes(
    uint32_t displayW, uint32_t displayH, uint32_t initialW, uint32_t initialH,
    uint32_t& outActiveSlot, uint32_t windowStart = 0xFFFFFFFFu) {

    uint32_t targetPercent = 100;
    if (displayW > 0 && initialW > 0) {
        targetPercent = (initialW * 100u + (displayW / 2u)) / displayW;
    }
    uint32_t targetIdx = findClosestResolutionPercentIndex(targetPercent);

    uint32_t startIdx = windowStart;
    if (startIdx > kNumAllResolutionPercents - 6) {
        int s = (int)targetIdx - 2;
        if (s < 0) s = 0;
        if (s > (int)kNumAllResolutionPercents - 6) s = (int)kNumAllResolutionPercents - 6;
        startIdx = (uint32_t)s;
    }

    outActiveSlot = (targetIdx >= startIdx && targetIdx < startIdx + 6) ? (targetIdx - startIdx) : 2;

    std::array<AndroidRenderMode, 6> modes{};
    for (size_t i = 0; i < 6; ++i) {
        uint32_t pct = kAllResolutionPercents[startIdx + i];
        uint32_t w, h;
        if (i == outActiveSlot && initialW > 0 && initialH > 0) {
            w = initialW;
            h = initialH;
        } else {
            w = std::max(320u, (displayW * pct / 100u) & ~1u);
            h = std::max(240u, (displayH * pct / 100u) & ~1u);
        }
        modes[i] = {w, h, pct};
    }
    return modes;
}

