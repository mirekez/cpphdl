#include "cpphdl.h"
#include <vector>
#include "TestConfig.h"

using SoftwareContainer = std::vector<uint32_t>;

class VectorTop : public cpphdl::Module
{
public:
    _PORT(bool) command_valid_in;
    _PORT(uint32_t) operation_in;
    _PORT(uint32_t) key_in;
    _PORT(uint32_t) value_in;
    _PORT(bool) command_ready_out;
    _PORT(bool) response_ready_in;
    _PORT(bool) response_valid_out;
    _PORT(uint32_t) status_out;
    _PORT(uint32_t) value_out;
    _PORT(uint32_t) size_out;

private:
#if HLS_HEAP
    HLS_BOUNDED(HLS_CAPACITY) SoftwareContainer* values = new SoftwareContainer;
#else
    HLS_BOUNDED(HLS_CAPACITY) SoftwareContainer values;
#endif
    cpphdl::reg<cpphdl::u1> pending_reg;
    cpphdl::reg<cpphdl::u32> status_reg, result_reg, count_reg;

public:
#if HLS_HEAP
    ~VectorTop() { delete values; }
#endif
    VectorTop() = default;
    VectorTop(const VectorTop&) = delete;
    VectorTop& operator=(const VectorTop&) = delete;

    void _assign()
    {
        command_ready_out = _ASSIGN(!pending_reg);
        response_valid_out = _ASSIGN((bool)pending_reg);
        status_out = _ASSIGN((uint32_t)status_reg);
        value_out = _ASSIGN((uint32_t)result_reg);
        size_out = _ASSIGN((uint32_t)count_reg);
    }
    void _work(bool reset)
    {
#if HLS_HEAP
        SoftwareContainer& data = *values;
#else
        SoftwareContainer& data = values;
#endif
        uint32_t operation, key, value, status, result;
        if (reset) {
            data.clear();
            pending_reg.clr(); status_reg.clr(); result_reg.clr(); count_reg.clr();
        } else if (pending_reg) {
            if (response_ready_in()) pending_reg._next = false;
        } else if (command_valid_in()) {
            operation = operation_in(); key = key_in(); value = value_in();
            status = 0; result = 0;
            if (operation == 3) result = data.size();
            else if (operation > 3) status = 3;
            else {
                if (operation == 0) {
                    if (data.size() == HLS_CAPACITY) status = 2;
                    else data.push_back(value);
                } else if (key >= data.size()) status = 1;
                else if (operation == 1) result = data[key];
                else data.erase(data.begin() + key);

            }
            pending_reg._next = true;
            status_reg._next = status;
            result_reg._next = result;
            count_reg._next = data.size();
        }
    }
    void _strobe()
    {
        pending_reg.strobe(); status_reg.strobe(); result_reg.strobe(); count_reg.strobe();
    }
};

#ifndef SYNTHESIS
#include "ContainerTest.h"

static void apply_vector_operation(SoftwareContainer& values, uint32_t operation,
                                   uint32_t index, uint32_t value,
                                   uint32_t& status, uint32_t& result)
{
    if (operation == 0) {
        if (values.size() == HLS_CAPACITY) status = 2;
        else values.push_back(value);
    } else if (index >= values.size()) status = 1;
    else if (operation == 1) result = values[index];
    else values.erase(values.begin() + index);
}

int main()
{
    return run_container_test<VectorTop>("Vector", apply_vector_operation);
}
#endif
