#include "cpphdl.h"
#include <array>
#include <cstdio>

using namespace cpphdl;

struct PackedAggregate
{
    logic<4> low;
    logic<4> high;

    template<typename T>
    PackedAggregate& operator=(T value)
    {
        const uint64_t raw = static_cast<uint64_t>(value);
        low = raw;
        high = raw >> 4;
        return *this;
    }

    logic<8> pack() const
    {
        return cat(high, low);
    }
};

static_assert(std::is_aggregate_v<PackedAggregate>);

static_assert([] {
    logic<64> narrow = 0;
    logic<129> wide = 0;
    sv_assign_bit(narrow, 63, 3);
    sv_assign_bit(wide, 128, 3);
    return narrow.get(63) == 1 && wide.get(128) == 1;
}());

template<size_t Width>
bool checkBitStores()
{
    logic<Width> expected;
    logic<Width> actual;
    // A bit write must retain all other physical bits, including padding.
    for (size_t byte = 0; byte < logic<Width>::SIZE; ++byte) {
        expected.bytes[byte] = actual.bytes[byte] = 0xa5;
    }
    for (size_t index = 0; index < Width; ++index) {
        for (unsigned value = 0; value < 4; ++value) {
            expected[index] = logic<8>(value);
            sv_assign_bit(actual, index, logic<8>(value));
            for (size_t byte = 0; byte < logic<Width>::SIZE; ++byte) {
                if (expected.bytes[byte] != actual.bytes[byte]) return false;
            }
        }
    }
    return true;
}

template<size_t Width>
bool checkLayoutCopies()
{
    array<3, logic<Width>, true> packed{};
    array<3, logic<Width>> unpacked{};
    for (size_t index = 0; index < 3; ++index) {
        logic<Width> element = index + 1;
        element.set(Width - 1, index & 1);
        packed[index] = element;
    }
    sv_assign_field(unpacked, packed);
    for (size_t index = 0; index < 3; ++index) {
        if (unpacked[index] != logic<Width>(packed[index])) return false;
    }
    reg<array<3, logic<Width>>> registered;
    registered._next = unpacked;
    registered.strobe();
    array<3, logic<Width>, true> roundTrip{};
    sv_assign_field(roundTrip, registered);
    return roundTrip.pack() == packed.pack();
}

// Packed-array field assignment previously repeated or narrowed scalar sources.
// SystemVerilog instead assigns the scalar to the complete packed destination value.
// Verify nonzero replication is rejected and zero assignment clears every element.
// Nested packed arrays also verify recursive overload lookup selects array assignment.
// sv_cast of std::array verifies array overloads are declared before its dependent call.
int main()
{
    // Field projections can change storage layout at an interface boundary.
    // A packed request vector must remain an indexed vector, not a broadcast.
    for (unsigned mask = 0; mask < 1024; ++mask) {
        array<10, logic<1>, true> requests = mask;
        array<10, logic<1>> wrapper{};
        sv_assign_field(wrapper, requests);
        for (size_t port = 0; port < 10; ++port) {
            if (uint64_t(wrapper[port]) != ((mask >> port) & 1)) {
                std::printf("packed/unpacked copy mask=%u port=%zu actual=%llu expected=%u\n",
                            mask, port, (unsigned long long)uint64_t(wrapper[port]),
                            (mask >> port) & 1);
                return 12;
            }
        }
        array<10, logic<1>, true> roundTrip{};
        sv_assign_field(roundTrip, wrapper);
        if (uint64_t(roundTrip.pack()) != mask) return 13;

        reg<array<10, logic<1>, true>> registeredRequests;
        registeredRequests._next = requests;
        registeredRequests.strobe();
        sv_assign_field(wrapper, registeredRequests);
        for (size_t port = 0; port < 10; ++port) {
            if (uint64_t(wrapper[port]) != ((mask >> port) & 1)) return 14;
        }
    }
    array<4, logic<4>, true> value;

    sv_assign_field(value, 3);

    const auto packed = value.pack();
    if (uint64_t(packed) != 0x0003ull) {
        std::printf("sv_assign_field packed array scalar result 0x%llx\n",
                    (unsigned long long)uint64_t(packed));
        return 1;
    }

    sv_assign_field(value, 0);
    if (uint64_t(value.pack()) != 0ull) {
        std::printf("sv_assign_field packed array zero result 0x%llx\n",
                    (unsigned long long)uint64_t(value.pack()));
        return 2;
    }

    array<2, array<2, logic<4>, true>, true> nested;
    sv_assign_field(nested, 3);
    if (uint64_t(nested.pack()) != 0x0003ull) {
        std::printf("sv_assign_field nested packed array scalar result 0x%llx\n",
                    (unsigned long long)uint64_t(nested.pack()));
        return 3;
    }

    const auto standard = sv_cast<std::array<int, 2>>(3);
    if (standard[0] != 3 || standard[1] != 3) {
        std::printf("sv_cast std::array did not select aggregate assignment\n");
        return 4;
    }

    // Generated packed structs are ordinary C++ aggregates with packed assignment.
    // Scalar zero must clear every field through value initialization, while nonzero
    // values must retain the packed assignment operator's bit distribution.
    PackedAggregate aggregate{logic<4>(0xf), logic<4>(0xf)};
    sv_assign_field(aggregate, 0);
    if (uint64_t(aggregate.pack()) != 0) {
        std::printf("sv_assign_field packed aggregate zero result 0x%llx\n",
                    (unsigned long long)uint64_t(aggregate.pack()));
        return 5;
    }
    sv_assign_field(aggregate, 0x21);
    if (uint64_t(aggregate.pack()) != 0x21) {
        std::printf("sv_assign_field packed aggregate scalar result 0x%llx\n",
                    (unsigned long long)uint64_t(aggregate.pack()));
        return 6;
    }

    // Matching register-backed arrays are copies, including nested shapes.
    // Compare individual fields: aggregate packing can hide a broadcast bug.
    using row_t = array<3, PackedAggregate>;
    using matrix_t = array<4, row_t>;
    reg<matrix_t> source;
    matrix_t destination;
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            source[row][column] = 17 * row + column + 1;
        }
    }
    sv_assign_field(destination, source);
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            if (uint64_t(destination[row][column].low) != uint64_t(source[row][column].low) ||
                uint64_t(destination[row][column].high) != uint64_t(source[row][column].high)) {
                return 7;
            }
        }
    }
    sv_assign_field(destination, 0x21);
    for (size_t row = 0; row < 4; ++row) {
        for (size_t column = 0; column < 3; ++column) {
            if (uint64_t(destination[row][column].pack()) != 0x21) return 8;
        }
    }
    reg<array<4, logic<4>, true>> packedRegister;
    packedRegister._next = 0x4321;
    packedRegister.strobe();
    sv_assign_field(value, packedRegister);
    if (uint64_t(value.pack()) != 0x4321) return 9;
    sv_assign_bit(value, 2, logic<4>(0xf));
    if (uint64_t(value.pack()) != 0x4f21) return 10;
    if (!checkBitStores<1>() || !checkBitStores<7>() || !checkBitStores<8>() ||
        !checkBitStores<9>() || !checkBitStores<15>() || !checkBitStores<16>() ||
        !checkBitStores<17>() || !checkBitStores<31>() || !checkBitStores<32>() ||
        !checkBitStores<33>() || !checkBitStores<63>() || !checkBitStores<64>() ||
        !checkBitStores<65>() || !checkBitStores<129>()) return 11;
    if (!checkLayoutCopies<1>() || !checkLayoutCopies<9>() ||
        !checkLayoutCopies<65>()) return 15;
    return 0;
}
