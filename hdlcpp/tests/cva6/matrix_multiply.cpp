static const int ROWS_A = 10;
static const int COLS_A_ROWS_B = 20;
static const int COLS_B = 10;

static int expected_value(int row, int col) {
    return (col + 2) * (210 * row + 2660);
}

int main() {
    int A[ROWS_A][COLS_A_ROWS_B];
    int B[COLS_A_ROWS_B][COLS_B];
    int C[ROWS_A][COLS_B] = {0};

    for (int i = 0; i < ROWS_A; ++i) {
        for (int j = 0; j < COLS_A_ROWS_B; ++j) {
            A[i][j] = i + j;
        }
    }

    for (int i = 0; i < COLS_A_ROWS_B; ++i) {
        for (int j = 0; j < COLS_B; ++j) {
            B[i][j] = (i + 1) * (j + 2);
        }
    }

    for (int i = 0; i < ROWS_A; ++i) {
        for (int k = 0; k < COLS_A_ROWS_B; ++k) {
            for (int j = 0; j < COLS_B; ++j) {
                C[i][j] += A[i][k] * B[k][j];
            }
        }
    }

    for (int i = 0; i < ROWS_A; ++i) {
        for (int j = 0; j < COLS_B; ++j) {
            if (C[i][j] != expected_value(i, j)) {
                return 1;
            }
        }
    }

    return 0;
}
