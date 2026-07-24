

#define ENABLE_RV32IA  // atomics
#define ENABLE_ZICSR   // CSR
#define ENABLE_TRAPS   // privilege modes and synchronous traps
#define ENABLE_ISR     // interrupt routing and CLINT timer
#define ENABLE_MMU_TLB // Sv32 address translation, TLB, and sfence.vma decode

#define L1_ICACHE_SIZE 4096
#define L1_DCACHE_SIZE 1024
#define L2_CACHE_SIZE 8192
#define L1_CACHE_ASSOCIATIONS 2
#define L2_CACHE_ASSOCIATIONS 4

#define CPUS_PER_L2_CACHE 4

#define BRANCH_PREDICTOR_ENTRIES 16
#define BRANCH_PREDICTOR_COUNTER_BITS 2
