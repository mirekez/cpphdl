#include <cpphdl.h>

using namespace cpphdl;

template<size_t WIDTH, unsigned BIAS = 3>
class StandaloneNumeric : public Module
{
public:
    _PORT(logic<WIDTH>) data_in;
    _PORT(logic<WIDTH>) data_out = _ASSIGN_COMB(value_comb_func());

private:
    logic<WIDTH> value_comb;
    logic<WIDTH>& value_comb_func()
    {
        value_comb = logic<WIDTH>(data_in() + BIAS);
        return value_comb;
    }
};

template<size_t WIDTH = 8, size_t COUNT = 2>
class StandaloneArray : public Module
{
public:
    _PORT(array<COUNT, logic<WIDTH>>) data_in;
    _PORT(array<COUNT, logic<WIDTH>>) data_out = _ASSIGN(data_in());
};

struct StandaloneWord
{
    uint16_t raw;
};

template<size_t COUNT = 2>
class StandaloneStructArray : public Module
{
public:
    _PORT(uint16_t) data_out = _ASSIGN_COMB(data_comb_func());

private:
    array<COUNT, StandaloneWord> words_comb;
    uint16_t data_comb;

    uint16_t& data_comb_func()
    {
        unsigned i;
        for (i = 0; i < COUNT; ++i) {
            words_comb[i].raw = i + 7;
        }
        data_comb = words_comb[0].raw;
        return data_comb;
    }
};

template<size_t BITS>
struct StandaloneIf : public Interface
{
    _PORT(logic<BITS>) data_in;
    _PORT(bool) ready_out;
};

template<size_t WIDTH = 8>
class StandaloneInterface : public Module
{
public:
    StandaloneIf<WIDTH> sink_in;
    StandaloneIf<WIDTH> source_out;

    void _assign()
    {
        source_out.data_in = _ASSIGN(sink_in.data_in());
        sink_in.ready_out = _ASSIGN(source_out.ready_out());
    }
};

// A type parameter still requires a concrete C++ specialization.
template<typename T>
class StandaloneTyped : public Module
{
public:
    _PORT(T) data_in;
    _PORT(T) data_out = _ASSIGN(data_in());
};

template<size_t WIDTH>
class StandaloneDependentBase : public StandaloneNumeric<WIDTH>
{
};

template<size_t WIDTH>
class StandaloneDependentChild : public Module
{
public:
    StandaloneNumeric<WIDTH> child;
};

#ifdef INSTANTIATE_NUMERIC
class StandaloneTop : public Module
{
public:
    StandaloneNumeric<12> child;
    StandaloneDependentBase<12> inherited;
    StandaloneDependentChild<12> hierarchy;
};
#endif
