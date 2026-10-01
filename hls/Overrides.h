#pragma once

// Replacements affect AST HLS conversion only. Native C++ keeps the original
// calls, so it remains an independent reference for the replacement's behavior.
#ifdef __clang__
#define HLS_OVERRIDE(target) [[clang::annotate("CPPHDL_HLS_OVERRIDE=" target)]]
#define HLS_OVERRIDE_BITS(target) [[clang::annotate("CPPHDL_HLS_OVERRIDE_BITS=" target)]]
#else
#define HLS_OVERRIDE(target)
#define HLS_OVERRIDE_BITS(target)
#endif
