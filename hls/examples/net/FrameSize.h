#pragma once
#include <cstdint>
#include "WordMath.h"

inline constexpr uint32_t HFT_MIN_FRAME_BYTES = 64;
inline constexpr uint32_t HFT_MAX_FRAME_BYTES = 100;

// Bits 0..7: length, bit 8: collecting. The other bits describe this beat:
// bit 9: store its bytes, bit 10: frame-size error, bit 11: complete frame.
// The next invocation receives only the low nine state bits.
struct HftFraming {
static constexpr uint32_t MIN_BYTES = 64;
static constexpr uint32_t MAX_BYTES = 100;
static uint32_t step(uint32_t state, uint32_t flags) {
    uint32_t count, used, result, total;
    bool collecting;
    count = flags >> 2;
    used = (flags & 1) ? 0 : state & 255;
    collecting = (flags & 1) || (state & 256);
    result = used;
    total = HftWordMath::add(used, count);
    if (collecting && count != 0 && !HftWordMath::less(4, count) && ((flags & 2) || count == 4)) {
        if (HftWordMath::less(MAX_BYTES, total)) result |= 1024;
        else {
            result = total | 512;
            if (flags & 2) {
                if (HftWordMath::less(total, MIN_BYTES)) result |= 1024;
                else result |= 2048;
            } else result |= 256;
        }
    }
    return result;
}
};
inline uint32_t hftFrameSizeStep(uint32_t state, uint32_t flags) { return HftFraming::step(state, flags); }
