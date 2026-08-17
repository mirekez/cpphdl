#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>

static volatile uint64_t memory_words[4] __attribute__((aligned(64)));

enum { MATRIX_SIZE = 16, MATRIX_ROUNDS = 8 };
static volatile uint64_t matrix_a[MATRIX_SIZE][MATRIX_SIZE];
static volatile uint64_t matrix_b[MATRIX_SIZE][MATRIX_SIZE];
static volatile uint64_t matrix_c[MATRIX_SIZE][MATRIX_SIZE];

static uint64_t rv64_add(uint64_t lhs, uint64_t rhs) {
  uint64_t result;
  __asm__ volatile("add %0, %1, %2" : "=r"(result) : "r"(lhs), "r"(rhs));
  return result;
}

static uint64_t rv64_mul(uint64_t lhs, uint64_t rhs) {
  uint64_t result;
  __asm__ volatile("mul %0, %1, %2" : "=r"(result) : "r"(lhs), "r"(rhs));
  return result;
}

static uint64_t rv64_divu(uint64_t lhs, uint64_t rhs) {
  uint64_t result;
  __asm__ volatile("divu %0, %1, %2" : "=r"(result) : "r"(lhs), "r"(rhs));
  return result;
}

static uint64_t rv64_sll(uint64_t value, uint64_t amount) {
  uint64_t result;
  __asm__ volatile("sll %0, %1, %2" : "=r"(result) : "r"(value), "r"(amount));
  return result;
}

static int64_t rv64_addiw(int64_t value) {
  int64_t result;
  __asm__ volatile("addiw %0, %1, 1" : "=r"(result) : "r"(value));
  return result;
}

static uint64_t rv64_load_store(uint64_t value) {
  uint64_t result;
  volatile uint64_t *address = &memory_words[0];
  __asm__ volatile(
      "sd %1, 0(%2)\n\t"
      "ld %0, 0(%2)"
      : "=r"(result), "+r"(value)
      : "r"(address)
      : "memory");
  return result;
}

static int check(const char *operation, uint64_t actual, uint64_t expected) {
  if (actual == expected)
    return 0;
  printf("ROCKET RV64 MMUL TEST FAILED: %s actual=0x%016" PRIx64
         " expected=0x%016" PRIx64 "\n",
         operation, actual, expected);
  return 1;
}

static int run_matrix_multiply(uint64_t *aggregate_result) {
  for (unsigned i = 0; i < MATRIX_SIZE; ++i) {
    for (unsigned j = 0; j < MATRIX_SIZE; ++j) {
      matrix_a[i][j] = (i * 17U + j * 13U + 1U) & 0xffU;
      matrix_b[i][j] = (i * 7U + j * 19U + 3U) & 0x7fU;
      matrix_c[i][j] = 0;
    }
  }

  printf("RV64 matrix workload: %ux%u, %u rounds\n",
         MATRIX_SIZE, MATRIX_SIZE, MATRIX_ROUNDS);

  uint64_t aggregate = 0;
  for (unsigned round = 0; round < MATRIX_ROUNDS; ++round) {
    for (unsigned i = 0; i < MATRIX_SIZE; ++i) {
      for (unsigned j = 0; j < MATRIX_SIZE; ++j) {
        uint64_t sum = 0;
        for (unsigned k = 0; k < MATRIX_SIZE; ++k)
          sum += matrix_a[i][k] * matrix_b[k][j];
        matrix_c[i][j] = sum;
      }
    }

    uint64_t checksum = UINT64_C(1469598103934665603);
    for (unsigned i = 0; i < MATRIX_SIZE; ++i) {
      for (unsigned j = 0; j < MATRIX_SIZE; ++j) {
        checksum ^= matrix_c[i][j];
        checksum *= UINT64_C(1099511628211);
      }
    }
    aggregate ^= checksum + round * UINT64_C(0x9e3779b97f4a7c15);

    if ((round & 1U) == 1U)
      printf("RV64 matrix progress: %u/%u rounds\n", round + 1U,
             MATRIX_ROUNDS);
  }

  if (check("matrix C[0][0]", matrix_c[0][0], UINT64_C(118408)) ||
      check("matrix C[15][15]", matrix_c[15][15], UINT64_C(114504)) ||
      check("matrix aggregate", aggregate, UINT64_C(0x7c4069f402f40058)))
    return 1;

  *aggregate_result = aggregate;
  return 0;
}

int main(void) {
  const uint64_t a = UINT64_C(0xfedcba9876543210);
  const uint64_t b = UINT64_C(0x0123456789abcdef);
  uint64_t signature = UINT64_C(0x525636344e415449);

  if (sizeof(uintptr_t) != 8) {
    printf("ROCKET RV64 MMUL TEST FAILED: sizeof(uintptr_t)=%u\n",
           (unsigned)sizeof(uintptr_t));
    return 1;
  }

  const uint64_t sum = rv64_add(a, b);
  const uint64_t product = rv64_mul(UINT64_C(0x123456789), UINT64_C(0x10203));
  const uint64_t quotient = rv64_divu(UINT64_C(0xfedcba9876543210), UINT64_C(0x12345));
  const uint64_t shifted = rv64_sll(UINT64_C(0x12345678), 40);
  const uint64_t loaded = rv64_load_store(a);
  const uint64_t word_sign_extended = (uint64_t)rv64_addiw(INT64_C(0x7fffffff));
  uint64_t matrix_aggregate = 0;

  if (check("add", sum, UINT64_C(0xffffffffffffffff)) ||
      check("mul", product, UINT64_C(0x1258f5c28489b)) ||
      check("divu", quotient, UINT64_C(0xe0004fa01c4d)) ||
      check("sll", shifted, UINT64_C(0x3456780000000000)) ||
      check("sd/ld", loaded, a) ||
      check("addiw", word_sign_extended, UINT64_C(0xffffffff80000000)) ||
      run_matrix_multiply(&matrix_aggregate))
    return 1;

  signature ^= sum;
  signature ^= product;
  signature ^= quotient;
  signature ^= shifted;
  signature ^= loaded;
  signature ^= word_sign_extended;
  signature ^= matrix_aggregate;

  printf("ROCKET RV64 MMUL TEST PASSED: signature=0x%016" PRIx64 "\n",
         signature);
  return 0;
}
