#pragma once
#include <cstdint>

// Reflected Ethernet CRC-32, polynomial 0xedb88320. Each byte transform is
// an XOR network, not a ROM lookup or a bit-at-a-time scheduled loop.
struct EthernetCrc {
static uint32_t byte(uint32_t crc, uint32_t value) {
    uint32_t x = crc ^ value;
    return (crc >> 8) ^
        ((((x & 1u) ? 0x77073096u : 0u) ^ ((x & 2u) ? 0xee0e612cu : 0u)) ^
         (((x & 4u) ? 0x076dc419u : 0u) ^ ((x & 8u) ? 0x0edb8832u : 0u))) ^
        ((((x & 16u) ? 0x1db71064u : 0u) ^ ((x & 32u) ? 0x3b6e20c8u : 0u)) ^
         (((x & 64u) ? 0x76dc4190u : 0u) ^ ((x & 128u) ? 0xedb88320u : 0u)));
}

// Columns of the reflected CRC-32 transform for 32 input bits. The XOR
// tree is balanced, so full words do not traverse four byte transforms.
static uint32_t fullWord(uint32_t crc, uint32_t value) {
    uint32_t x, g0, g1, g2, g3, g4, g5, g6, g7;
    x = crc ^ value;
    g0 = (((x & 0x1u) ? 0xb8bc6765u : 0u) ^ ((x & 0x2u) ? 0xaa09c88bu : 0u)) ^
        (((x & 0x4u) ? 0x8f629757u : 0u) ^ ((x & 0x8u) ? 0xc5b428efu : 0u));
    g1 = (((x & 0x10u) ? 0x5019579fu : 0u) ^ ((x & 0x20u) ? 0xa032af3eu : 0u)) ^
        (((x & 0x40u) ? 0x9b14583du : 0u) ^ ((x & 0x80u) ? 0xed59b63bu : 0u));
    g2 = (((x & 0x100u) ? 0x01c26a37u : 0u) ^ ((x & 0x200u) ? 0x0384d46eu : 0u)) ^
        (((x & 0x400u) ? 0x0709a8dcu : 0u) ^ ((x & 0x800u) ? 0x0e1351b8u : 0u));
    g3 = (((x & 0x1000u) ? 0x1c26a370u : 0u) ^ ((x & 0x2000u) ? 0x384d46e0u : 0u)) ^
        (((x & 0x4000u) ? 0x709a8dc0u : 0u) ^ ((x & 0x8000u) ? 0xe1351b80u : 0u));
    g4 = (((x & 0x10000u) ? 0x191b3141u : 0u) ^ ((x & 0x20000u) ? 0x32366282u : 0u)) ^
        (((x & 0x40000u) ? 0x646cc504u : 0u) ^ ((x & 0x80000u) ? 0xc8d98a08u : 0u));
    g5 = (((x & 0x100000u) ? 0x4ac21251u : 0u) ^ ((x & 0x200000u) ? 0x958424a2u : 0u)) ^
        (((x & 0x400000u) ? 0xf0794f05u : 0u) ^ ((x & 0x800000u) ? 0x3b83984bu : 0u));
    g6 = (((x & 0x1000000u) ? 0x77073096u : 0u) ^ ((x & 0x2000000u) ? 0xee0e612cu : 0u)) ^
        (((x & 0x4000000u) ? 0x076dc419u : 0u) ^ ((x & 0x8000000u) ? 0x0edb8832u : 0u));
    g7 = (((x & 0x10000000u) ? 0x1db71064u : 0u) ^ ((x & 0x20000000u) ? 0x3b6e20c8u : 0u)) ^
        (((x & 0x40000000u) ? 0x76dc4190u : 0u) ^ ((x & 0x80000000u) ? 0xedb88320u : 0u));
    return ((g0 ^ g1) ^ (g2 ^ g3)) ^ ((g4 ^ g5) ^ (g6 ^ g7));
}
static uint32_t word(uint32_t crc, uint32_t value, uint32_t bytes) {
    uint32_t c1, c2, c3, c4;
    c1 = byte(crc, value);
    c2 = byte(c1, value >> 8);
    c3 = byte(c2, value >> 16);
    c4 = fullWord(crc, value);
    if (bytes == 0) return crc;
    if (bytes == 1) return c1;
    if (bytes == 2) return c2;
    if (bytes == 3) return c3;
    return c4;
}
};
inline uint32_t ethernetCrcByte(uint32_t crc, uint32_t value) { return EthernetCrc::byte(crc, value); }
inline uint32_t ethernetCrcWord(uint32_t crc, uint32_t value, uint32_t bytes) {
    return EthernetCrc::word(crc, value, bytes);
}
