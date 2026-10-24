#include <stdint.h>

extern "C" void printstr(const char *text);

#ifndef MATRIX_SIZE
#define MATRIX_SIZE 32
#endif
#ifndef MATRIX_REPEATS
#define MATRIX_REPEATS 1
#endif

static_assert(MATRIX_SIZE > 0 && MATRIX_SIZE <= 128);
static_assert(MATRIX_REPEATS > 0 && MATRIX_REPEATS <= 1024);

static constexpr unsigned Size = MATRIX_SIZE;
static constexpr unsigned Repeats = MATRIX_REPEATS;
alignas(64) static int32_t matrix_a[Size][Size];
alignas(64) static int32_t matrix_b[Size][Size];
alignas(64) static int32_t matrix_c[Size][Size];

static void __attribute__((noinline)) initialize(unsigned offset)
{
    for (unsigned row = 0; row < Size; ++row) {
        for (unsigned column = 0; column < Size; ++column) {
            matrix_a[row][column] = row + column + offset;
            matrix_b[row][column] = (row + 1) * (column + 2);
            matrix_c[row][column] = 0;
        }
    }
}

static void __attribute__((noinline)) multiply()
{
    for (unsigned row = 0; row < Size; ++row) {
        for (unsigned inner = 0; inner < Size; ++inner) {
            int32_t left = matrix_a[row][inner];
            for (unsigned column = 0; column < Size; ++column) {
                matrix_c[row][column] += left * matrix_b[inner][column];
            }
        }
    }
}

static char *append_text(char *output, const char *text)
{
    while (*text) {
        *output++ = *text++;
    }
    return output;
}

static char *append_decimal(char *output, uint32_t value)
{
    char digits[10];
    unsigned count = 0;
    do {
        digits[count++] = char('0' + value % 10);
        value /= 10;
    } while (value);
    while (count) {
        *output++ = digits[--count];
    }
    return output;
}

int main()
{
    uint32_t checksum = 2166136261u;
    constexpr uint32_t triangular = Size * (Size + 1) / 2;
    constexpr uint32_t quadratic = Size * (Size - 1) * (Size + 1) / 3;
    for (unsigned repeat = 0; repeat < Repeats; ++repeat) {
        unsigned offset = repeat % 17;
        initialize(offset);
        multiply();
        for (unsigned row = 0; row < Size; ++row) {
            for (unsigned column = 0; column < Size; ++column) {
                uint32_t expected = (column + 2) * ((row + offset) * triangular + quadratic);
                if (matrix_c[row][column] != int32_t(expected)) {
                    printstr("FAILED\n");
                    return 1;
                }
                checksum = (checksum ^ uint32_t(matrix_c[row][column])) * 16777619u;
            }
        }
    }
    char message[128];
    char *output = append_text(message, "CVA6_MMUL size=");
    output = append_decimal(output, Size);
    output = append_text(output, " repeats=");
    output = append_decimal(output, Repeats);
    output = append_text(output, " macs=");
    output = append_decimal(output, Size * Size * Size * Repeats);
    output = append_text(output, " checksum=0x");
    const char digits[] = "0123456789abcdef";
    for (unsigned nibble = 8; nibble > 0; --nibble) {
        *output++ = digits[(checksum >> ((nibble - 1) * 4)) & 15];
    }
    output = append_text(output, "\nPASSED\n");
    *output = '\0';
    printstr(message);
    return 0;
}
