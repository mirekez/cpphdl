extern "C" void printstr(const char *s);

static int __attribute__((noinline)) report_success()
{
    printstr("PASSED\n");
    return 0;
}

int main()
{
    volatile int A00 = 0;
    volatile int A01 = 1;
    volatile int A02 = 2;
    volatile int A10 = 1;
    volatile int A11 = 2;
    volatile int A12 = 3;

    volatile int B00 = 2;
    volatile int B01 = 3;
    volatile int B10 = 4;
    volatile int B11 = 6;
    volatile int B20 = 6;
    volatile int B21 = 9;

    volatile int C00 = A00 * B00 + A01 * B10 + A02 * B20;
    volatile int C01 = A00 * B01 + A01 * B11 + A02 * B21;
    volatile int C10 = A10 * B00 + A11 * B10 + A12 * B20;
    volatile int C11 = A10 * B01 + A11 * B11 + A12 * B21;

    if (C00 != 16) {
        return 1;
    }
    if (C01 != 24) {
        return 1;
    }
    if (C10 != 28) {
        return 1;
    }
    if (C11 != 42) {
        return 1;
    }

    return report_success();
}
