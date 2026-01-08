#define PRINT(a) *(char*)0x11223344 = (a)

void print_dec(unsigned val);

int main()
{
    int A[10][10] = {{4,8,0,4,3,7,4,8,3,6},
                     {2,5,3,5,7,8,4,3,6,7},
                     {3,6,3,6,8,9,4,2,5,7},
                     {2,5,6,3,2,5,6,8,8,4},
                     {1,5,2,5,9,0,6,4,2,5},
                     {3,5,5,6,6,3,3,3,5,3},
                     {3,5,2,5,6,8,9,5,8,8},
                     {5,3,4,6,7,8,6,4,3,5},
                     {7,3,2,5,7,8,4,6,6,4},
                     {1,3,5,7,4,7,2,6,4,6}};

    int B[10][10] = {{9,6,4,3,2,4,2,5,6,7},
                     {9,6,4,3,4,6,4,3,2,4},
                     {0,7,4,3,2,5,3,2,2,5},
                     {7,4,2,5,4,6,7,8,9,9},
                     {8,6,4,3,2,5,3,5,2,5},
                     {9,6,4,3,2,5,2,2,4,6},
                     {8,5,3,3,2,5,6,8,8,4},
                     {9,6,5,4,3,2,1,3,5,6},
                     {7,5,4,3,2,5,6,8,8,5},
                     {7,5,4,3,2,6,7,3,2,2}};

    int C[10][10];
    for (int i=0; i < 10; ++i) {
        for (int k=0; k < 10; ++k) {
            C[i][k] = 0;
            for (int j=0; j < 10; ++j) {
                C[i][k] += A[i][j] * B[j][k];
            }
            print_dec(C[i][k]);
        }
        PRINT('\n');
    }

    while (1)
        ;
    return 0;
}

void *memset(void *dest, int ch, unsigned long count)
{
    for (int i=0; i < count; ++i) {
        ((unsigned char*)dest)[i] = ch;
    }
    return dest;
}

void *memcpy(void *dest, void *src, unsigned long count)
{
    for (int i=0; i < count; ++i) {
        ((unsigned char*)dest)[i] = ((unsigned char*)src)[i];
    }
    return dest;
}

void print_dec(unsigned val)
{
    unsigned pow = 1;
    while (pow < val) {
        pow *= 10;
    }
    pow /= 10;
    while (pow >= 1) {
        PRINT(val/pow + 0x30);
        val -= val/pow*pow;
        pow /= 10;
    }
    PRINT(' ');
}
