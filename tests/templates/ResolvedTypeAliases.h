#include <cpphdl.h>
#include <type_traits>

template<class T>
using AliasIdentity = T;

// Like File/FileStorage, the selected type is a builtin, not a hardware
// specialization named after std::conditional_t and its arguments.
template<unsigned WIDTH>
class ResolvedTypeAliases : public cpphdl::Module {
public:
    using DTYPE = std::conditional_t<(WIDTH <= 32), uint32_t, uint64_t>;
    using chained_t = AliasIdentity<DTYPE>;
    using wide_t = std::enable_if_t<true, uint64_t>;
    using signed_t = std::remove_cv_t<const int64_t>;
    using vector_t = AliasIdentity<cpphdl::logic<WIDTH>>;

    _PORT(DTYPE) data_in;
    _PORT(chained_t) result_out = _ASSIGN(data_in() + 1u);
    _PORT(wide_t) wide_out = _ASSIGN(uint64_t(data_in()));
    _PORT(signed_t) signed_out = _ASSIGN(int64_t(-7));
};

#ifndef ALIAS_WIDTH
#define ALIAS_WIDTH 32
#endif
ResolvedTypeAliases<ALIAS_WIDTH> cpphdl_top;
