#pragma once

#ifndef TRIBE_CFG_RV32IA
#define TRIBE_CFG_RV32IA 1
#endif
#ifndef TRIBE_CFG_ISR
#define TRIBE_CFG_ISR 1
#endif
#ifndef TRIBE_CFG_MMU_TLB
#define TRIBE_CFG_MMU_TLB 1
#endif

#if TRIBE_CFG_RV32IA
#define ENABLE_RV32IA  // atomics
#endif
#define ENABLE_ZICSR   // CSR
#define ENABLE_TRAPS   // privilege modes and synchronous traps
#if TRIBE_CFG_ISR
#define ENABLE_ISR     // interrupt routing and CLINT timer
#endif
#if TRIBE_CFG_MMU_TLB
#define ENABLE_MMU_TLB // Sv32 address translation, TLB, and sfence.vma decode
#endif

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
