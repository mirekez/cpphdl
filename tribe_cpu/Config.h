#pragma once

#define ENABLE_ZICSR   // CSR
#define ENABLE_TRAPS   // privilege modes and synchronous traps

#define L1_ICACHE_SIZE 2048
#define L1_DCACHE_SIZE 1024
#define L2_CACHE_SIZE (64 * 1024)
#define L1_CACHE_ASSOCIATIONS 2
#define L2_CACHE_ASSOCIATIONS 4

#define CPUS_PER_L2_CACHE 4

#define BRANCH_PREDICTOR_ENTRIES 16
#define BRANCH_PREDICTOR_COUNTER_BITS 2

#define CPU_CLK_MULTIPLIER 2

#if CPU_CLK_MULTIPLIER < 1
#error "CPU_CLK_MULTIPLIER must be a positive integer"
#endif
