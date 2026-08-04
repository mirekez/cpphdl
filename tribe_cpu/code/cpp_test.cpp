#include <algorithm>
#include <array>
#include <cstdint>
#include <numeric>
#include <new>
#include <string>
#include <vector>

#include "uart.h"

static int data_value = 17;
static int bss_value;
static int ctor_value;

struct GlobalCtor
{
    GlobalCtor()
    {
        ctor_value = data_value + 25;
    }
};

static GlobalCtor global_ctor;

struct Accumulator
{
    virtual int add(int value) = 0;
    virtual ~Accumulator() = default;
};

struct ScaleAccumulator final : Accumulator
{
    int scale;

    explicit ScaleAccumulator(int scale_in)
        : scale(scale_in)
    {
    }

    int add(int value) override
    {
        return value * scale + 1;
    }
};

static int use_vector()
{
    std::vector<int> values;
    for (int i = 0; i < 8; ++i) {
        values.push_back(i * 3 + 1);
    }
    std::reverse(values.begin(), values.end());
    return std::accumulate(values.begin(), values.end(), 0);
}

static int use_string()
{
    std::string text = "tri";
    text += "be";
    text.push_back('-');
    text += "cpp";
    return (int)text.size() + (text == "tribe-cpp" ? 100 : 0);
}

static int use_new_delete()
{
    int* scalar = new int(31);
    int scalar_value = *scalar;
    delete scalar;

    int* array = new int[5];
    for (int i = 0; i < 5; ++i) {
        array[i] = i + 2;
    }
    int sum = 0;
    for (int i = 0; i < 5; ++i) {
        sum += array[i];
    }
    delete[] array;

    Accumulator* acc = new ScaleAccumulator(4);
    int virtual_value = acc->add(6);
    delete acc;

    return scalar_value + sum + virtual_value;
}

int main()
{
    std::array<int, 4> fixed = {4, 1, 3, 2};
    std::sort(fixed.begin(), fixed.end());
    int fixed_sum = 0;
    for (int value : fixed) {
        fixed_sum += value;
    }

    const int vector_sum = use_vector();
    const int string_score = use_string();
    const int heap_score = use_new_delete();
    const int ok = (bss_value == 0)
        && (data_value == 17)
        && (ctor_value == 42)
        && (fixed_sum == 10)
        && (vector_sum == 92)
        && (string_score == 109)
        && (heap_score == 76);

    tribe_uart_puts("cpp-runtime\n");
    tribe_uart_puts("bss=");
    tribe_uart_put_i32(bss_value);
    tribe_uart_puts(" data=");
    tribe_uart_put_i32(data_value);
    tribe_uart_puts(" ctor=");
    tribe_uart_put_i32(ctor_value);
    tribe_uart_putc('\n');
    tribe_uart_puts("array=");
    tribe_uart_put_i32(fixed_sum);
    tribe_uart_puts(" vector=");
    tribe_uart_put_i32(vector_sum);
    tribe_uart_puts(" string=");
    tribe_uart_put_i32(string_score);
    tribe_uart_puts(" heap=");
    tribe_uart_put_i32(heap_score);
    tribe_uart_putc('\n');
    tribe_uart_puts(ok ? "PASS\n" : "FAIL\n");

    return ok ? 0 : 1;
}
