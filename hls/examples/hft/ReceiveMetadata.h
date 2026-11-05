#pragma once
#include "FrameSize.h"
#include "EthernetCrc.h"

// High word: frame state/events (FrameSize.h), plus CRC error in bit 12.
// Low word: CRC state for the next accepted word. Size errors take precedence.
inline uint64_t hftReceiveMetadata(uint32_t state, uint32_t crc, uint32_t flags, uint32_t word) {
    uint32_t frame = hftFrameSizeStep(state, flags);
    uint32_t checksum = (flags & 1) ? 0xffffffffu : crc;
    if (frame & 512) checksum = ethernetCrcWord(checksum, word, flags >> 2);
    if ((frame & 2048) && checksum != 0xdebb20e3u) frame |= 4096;
    return (uint64_t(frame) << 32) | checksum;
}
