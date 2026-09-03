#pragma once

#include "cpphdl.h"

using namespace cpphdl;

// Storage-only leaf used by File.  The C++ model retains the straightforward
// architectural array, while FPGA RTL may use a synthesis-specific physical
// implementation.  Register-file bypassing and all CPU control stay in File.
template<size_t MEM_WIDTH, size_t MEM_DEPTH>
class [[clang::annotate("CPPHDL_REPLACEMENT_FILE=FileStoragePrimitive.sv;")]]
FileStorage : public Module
{
    using DTYPE = std::conditional_t<(MEM_WIDTH <= 32), uint32_t, uint64_t>;

public:
    _PORT(uint8_t) write_addr_in;
    _PORT(bool) write_in;
    _PORT(DTYPE) write_data_in;
    _PORT(uint8_t) write2_addr_in;
    _PORT(bool) write2_in;
    _PORT(DTYPE) write2_data_in;
    _PORT(uint8_t) read_addr0_in;
    _PORT(uint8_t) read_addr1_in;
    _PORT(DTYPE) reset_x10_in;
    _PORT(DTYPE) reset_x11_in;
    _PORT(DTYPE) read_data0_out = _ASSIGN_COMB(read_data0_comb_func());
    _PORT(DTYPE) read_data1_out = _ASSIGN_COMB(read_data1_comb_func());
    _PORT(DTYPE) x1_out = _ASSIGN_COMB(x1_comb_func());
    _PORT(DTYPE) x10_out = _ASSIGN_COMB(x10_comb_func());
    _PORT(DTYPE) x11_out = _ASSIGN_COMB(x11_comb_func());
    _PORT(DTYPE) x16_out = _ASSIGN_COMB(x16_comb_func());
    _PORT(DTYPE) x17_out = _ASSIGN_COMB(x17_comb_func());

private:
    memory<u32, MEM_WIDTH / 32, MEM_DEPTH> buffer;

    _LAZY_COMB(read_data0_comb, DTYPE)
        return read_data0_comb = (DTYPE)buffer[read_addr0_in()];
    }

    _LAZY_COMB(read_data1_comb, DTYPE)
        return read_data1_comb = (DTYPE)buffer[read_addr1_in()];
    }

    _LAZY_COMB(x1_comb, DTYPE)
        return x1_comb = (DTYPE)buffer[1];
    }

    _LAZY_COMB(x10_comb, DTYPE)
        return x10_comb = (DTYPE)buffer[10];
    }

    _LAZY_COMB(x11_comb, DTYPE)
        return x11_comb = (DTYPE)buffer[11];
    }

    _LAZY_COMB(x16_comb, DTYPE)
        return x16_comb = (DTYPE)buffer[16];
    }

    _LAZY_COMB(x17_comb, DTYPE)
        return x17_comb = (DTYPE)buffer[17];
    }

public:
    void _work(bool reset)
    {
        size_t i;
        if (reset) {
            for (i = 0; i < MEM_DEPTH; ++i) {
                buffer[i] = 0;
            }
            buffer[10] = reset_x10_in();
            buffer[11] = reset_x11_in();
        }
        if (write_in()) {
            buffer[write_addr_in()] = write_data_in();
        }
        if (write2_in()) {
            buffer[write2_addr_in()] = write2_data_in();
        }
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        buffer.apply(checkpoint_fd);
    }

    void _assign() {}
};
