# Provenance

The transformer structure and Q16.48 arithmetic are adapted from the supplied
`intllm/src/qwen_int.cpp`, `intllm/src/qmath.hpp`, and
`intllm/third_party/fp_math.h`.

The inverse-square-root algorithm is derived from Nenad Micic's int-llm,
Copyright 2026 Nenad Micic, licensed under Apache-2.0. The original license and
copyright notice are retained in [LICENSE.int-llm](LICENSE.int-llm).
The implementation here uses native `__int128` instead of the original library's
portable wide-integer abstraction. Exponential range reduction and SiLU follow
the supplied Qwen adaptation. No tokenizer, model weights, or trained model
files are redistributed. Test weights are generated deterministically.

These sources are marked Apache-2.0. Changes include templated dimensions,
explicit contiguous weight offsets, an injectable dot-product implementation,
and a DDR-backed pipelined product module.
