// SPDX-License-Identifier: Apache-2.0
#pragma once
#include "IntegerMath.h"

namespace integer_llm {

// Small causal GQA transformer for tractable RTL regressions. The arithmetic
// and tensor ordering follow intllm's Qwen runner; these are not trained weights.
struct SmallModel {
    static constexpr unsigned dim = 4, inter = 8, layers = 1;
    static constexpr unsigned heads = 2, kvheads = 1, head = dim / heads;
    static constexpr unsigned kvdim = kvheads * head, context = 4, vocab = 8;
};
struct QwenModel {
    static constexpr unsigned dim = 896, inter = 4864, layers = 24;
    static constexpr unsigned heads = 14, kvheads = 2, head = dim / heads;
    static constexpr unsigned kvdim = kvheads * head, context = 2048, vocab = 151936;
};

template<class C> struct Layout {
    static constexpr unsigned embedding = 0, final_norm = C::vocab * C::dim;
    static constexpr unsigned layer_start = final_norm + C::dim;
    static constexpr unsigned in = 0, q = in + C::dim, k = q + C::dim * C::dim;
    static constexpr unsigned v = k + C::kvdim * C::dim, qb = v + C::kvdim * C::dim;
    static constexpr unsigned kb = qb + C::dim, vb = kb + C::kvdim;
    static constexpr unsigned o = vb + C::kvdim, post = o + C::dim * C::dim;
    static constexpr unsigned gate = post + C::dim, up = gate + C::inter * C::dim;
    static constexpr unsigned down = up + C::inter * C::dim;
    static constexpr unsigned layer_words = down + C::dim * C::inter;
    static constexpr unsigned rope = layer_start + C::layers * layer_words;
    static constexpr unsigned words = rope + C::context * C::head;
};

struct NativeDot {
    Q operator()(const Q* weights, const Q* values, unsigned count) {
        UWide sum = 0;
        for (unsigned i = 0; i < count; ++i) sum = accum(sum, weights[i], values[i]);
        return Q(Wide(sum) >> 48);
    }
};

template<class C = SmallModel> class Transformer {
    static_assert(C::dim > 0 && C::inter > 0 && C::layers > 0 && C::context > 0 && C::vocab > 0);
    static_assert(C::heads > 0 && C::kvheads > 0 && C::heads % C::kvheads == 0);
    static_assert(C::dim == C::heads * C::head && C::head % 2 == 0);
    static_assert(C::kvdim == C::kvheads * C::head);
    using L = Layout<C>;
    Q keys[C::layers][C::context][C::kvdim]{};
    Q values[C::layers][C::context][C::kvdim]{};
    unsigned position = 0;

    template<class Dot> static void matvec(Q* out, const Q* weights, const Q* x,
                                           unsigned rows, unsigned cols, const Q* bias, Dot& dot) {
        for (unsigned row = 0; row < rows; ++row)
            out[row] = dot(weights + row * cols, x, cols) + (bias ? bias[row] : 0);
    }
    static void norm(Q* out, const Q* x, const Q* weights) {
        Wide sum = 0;
        for (unsigned i = 0; i < C::dim; ++i) sum += Wide(x[i]) * x[i];
        Q scale = inverse_sqrt(Q((sum >> 48) / C::dim) + 281474976);
        for (unsigned i = 0; i < C::dim; ++i) out[i] = multiply(multiply(x[i], scale), weights[i]);
    }
    static void rotate(Q* x, unsigned heads, const Q* table) {
        for (unsigned h = 0; h < heads; ++h)
            for (unsigned j = 0; j < C::head / 2; ++j) {
                Q a = x[h * C::head + j], b = x[h * C::head + j + C::head / 2];
                Q c = table[2 * j], s = table[2 * j + 1];
                x[h * C::head + j] = multiply(a, c) - multiply(b, s);
                x[h * C::head + j + C::head / 2] = multiply(b, c) + multiply(a, s);
            }
    }
    void attention(Q* out, const Q* q, unsigned layer) {
        Q scores[C::context];
        Q scale = inverse_sqrt(Q(C::head) * one);
        for (unsigned h = 0; h < C::heads; ++h) {
            unsigned kh = h / (C::heads / C::kvheads);
            Q maximum = INT64_MIN, sum = 0;
            for (unsigned t = 0; t <= position; ++t) {
                Wide dot = 0;
                for (unsigned d = 0; d < C::head; ++d)
                    dot += Wide(q[h * C::head + d]) * keys[layer][t][kh * C::head + d];
                scores[t] = multiply(Q(dot >> 48), scale);
                if (scores[t] > maximum) maximum = scores[t];
            }
            for (unsigned t = 0; t <= position; ++t) { scores[t] = exp_negative(scores[t] - maximum); sum += scores[t]; }
            for (unsigned d = 0; d < C::head; ++d) out[h * C::head + d] = 0;
            for (unsigned t = 0; t <= position; ++t) {
                Q probability = divide(scores[t], sum);
                for (unsigned d = 0; d < C::head; ++d)
                    out[h * C::head + d] += multiply(probability, values[layer][t][kh * C::head + d]);
            }
        }
    }
public:
    void reset() { position = 0; } // Old KV entries are outside the causal range until overwritten.

    template<class Dot> bool forward(uint32_t token, const Q* weights, Q* logits, uint32_t& answer, Dot& dot) {
        Q hidden[C::dim], normal[C::dim], q[C::dim], k[C::kvdim], v[C::kvdim];
        Q attended[C::dim], output[C::dim], gate[C::inter], up[C::inter];
        if (token >= C::vocab || position >= C::context) return false;
        for (unsigned i = 0; i < C::dim; ++i) hidden[i] = weights[L::embedding + token * C::dim + i];
        for (unsigned l = 0; l < C::layers; ++l) {
            const Q* w = weights + L::layer_start + l * L::layer_words;
            norm(normal, hidden, w + L::in);
            matvec(q, w + L::q, normal, C::dim, C::dim, w + L::qb, dot);
            matvec(k, w + L::k, normal, C::kvdim, C::dim, w + L::kb, dot);
            matvec(v, w + L::v, normal, C::kvdim, C::dim, w + L::vb, dot);
            rotate(q, C::heads, weights + L::rope + position * C::head);
            rotate(k, C::kvheads, weights + L::rope + position * C::head);
            for (unsigned i = 0; i < C::kvdim; ++i) { keys[l][position][i] = k[i]; values[l][position][i] = v[i]; }
            attention(attended, q, l);
            matvec(output, w + L::o, attended, C::dim, C::dim, nullptr, dot);
            for (unsigned i = 0; i < C::dim; ++i) hidden[i] += output[i];
            norm(normal, hidden, w + L::post);
            matvec(gate, w + L::gate, normal, C::inter, C::dim, nullptr, dot);
            matvec(up, w + L::up, normal, C::inter, C::dim, nullptr, dot);
            for (unsigned i = 0; i < C::inter; ++i) gate[i] = multiply(silu(gate[i]), up[i]);
            matvec(output, w + L::down, gate, C::dim, C::inter, nullptr, dot);
            for (unsigned i = 0; i < C::dim; ++i) hidden[i] += output[i];
        }
        norm(normal, hidden, weights + L::final_norm);
        matvec(logits, weights + L::embedding, normal, C::vocab, C::dim, nullptr, dot);
        answer = 0;
        for (unsigned i = 1; i < C::vocab; ++i) if (logits[i] > logits[answer]) answer = i;
        ++position;
        return true;
    }
};
}
