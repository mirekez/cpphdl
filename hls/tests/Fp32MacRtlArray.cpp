#include "../math/Fp32MacRtlPipeline.h"
#ifndef MAC_ROWS
#define MAC_ROWS 4
#endif
#ifndef MAC_COLS
#define MAC_COLS 4
#endif
#ifndef MAC_LATENCY
#define MAC_LATENCY 48
#endif
#ifdef MAC_PRIMITIVE_ONLY
Fp32Mac mac_replacement;
#elif defined(MAC_PIPELINE_ONLY)
Fp32MacRtlPipeline<MAC_LATENCY> mac_replacement;
#else
Fp32MacRtlArray<MAC_ROWS,MAC_COLS,MAC_LATENCY> mac_replacement;
#endif
