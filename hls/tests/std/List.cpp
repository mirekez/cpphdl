#include "cpphdl.h"
#include <list>
#include "TestConfig.h"

using SoftwareContainer = std::list<uint32_t>;

class ListTop : public cpphdl::Module
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
    HLS_BOUNDED(HLS_CAPACITY) SoftwareContainer values;
    cpphdl::reg<cpphdl::u1> pending_reg;
    cpphdl::reg<cpphdl::u32> status_reg, result_reg, count_reg;

public:
    ListTop() = default;
    ListTop(const ListTop&) = delete;
    ListTop& operator=(const ListTop&) = delete;

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
        SoftwareContainer& data = values;
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
                else {
                    auto it = data.begin();
                    std::advance(it, key);
                    if (operation == 1) result = *it;
                    else data.erase(it);
                }

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

static void apply_list_operation(SoftwareContainer& values, uint32_t operation,
                                 uint32_t index, uint32_t value,
                                 uint32_t& status, uint32_t& result)
{
    if (operation == 0) {
        if (values.size() == HLS_CAPACITY) status = 2;
        else values.push_back(value);
    } else if (index >= values.size()) status = 1;
    else {
        auto it = values.begin();
        std::advance(it, index);
        if (operation == 1) result = *it;
        else values.erase(it);
    }
}

int main()
{
    return run_container_test<ListTop>("List", apply_list_operation);
}
#endif
