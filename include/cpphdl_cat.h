#pragma once

#include <vector>
#include <utility>
#include <tuple>

#include "cpphdl_logic.h"

namespace cpphdl
{


template<size_t... N>
constexpr size_t SUM() {
    return (N + ...);
}

template<size_t... N>
struct cat : logic<SUM<N...>()>
{
    template <typename Func, size_t... Indices>
    void forEachReverse(Func&& f, std::tuple<logic<N>...> &data, std::index_sequence<Indices...>) {
        ((f(std::get<sizeof...(Indices) - 1 - Indices>(data))), ...);
    }

    template<size_t... Indices>
    cat(logic<N>... logics)
    {
        size_t offset = 0;
        auto process_logic = [&](auto& bs) {
            if (offset%8 == 0) {
                memcpy((char*)this + offset/8, bs.bytes, sizeof(bs.bytes));
            }
            else
            for (size_t i = 0; i < bs.size(); ++i) {
                (*static_cast<logic<SUM<N...>()>*>(this)).set(SUM<N...>() - offset - i - 1, bs[i]);
            }
            offset += bs.size();
        };

        auto data = std::make_tuple(logics...);

        forEachReverse(process_logic, data, std::make_index_sequence<sizeof...(logics)>());
    }

    // !!!

    const cat& operator=(const cat& cat)
    {
        memcpy(this->bytes, cat.bytes, std::min(sizeof(this->bytes),sizeof(cat.bytes)));
        return *this;
    }
};


}
