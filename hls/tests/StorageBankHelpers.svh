// Test adapters reconstruct the logical byte array; the DUT uses separate
// bank states. All read/write operations below call the generated helpers.
`define BANK_WRITE(W, B) banks[B] = dut.hls_bank_write_``W``_``B(banks[B], address, value, next_fault);
`define STORAGE_ADAPTER(W) \
    function static logic [MEM_BYTES*8+31:0] write_``W( \
        input logic [(1 << $clog2(MEM_BYTES))-1:0][7:0] old, input logic [63:0] address, \
        input logic [W-1:0] value, input logic [31:0] fault, \
        input logic [7:0][(1 << $clog2(MEM_BYTES/8))-1:0][7:0] banks = '0, \
        input logic [31:0] next_fault = 0); \
        for (int unsigned i = 0; i < MEM_BYTES; ++i) banks[i%8][i/8] = old[i]; \
        next_fault = fault; \
        if (next_fault == 0 && !dut.hls_storage_address_valid(address, W/8)) next_fault = 3; \
        `BANK_WRITE(W, 0) `BANK_WRITE(W, 1) `BANK_WRITE(W, 2) `BANK_WRITE(W, 3) \
        `BANK_WRITE(W, 4) `BANK_WRITE(W, 5) `BANK_WRITE(W, 6) `BANK_WRITE(W, 7) \
        for (int unsigned i = 0; i < MEM_BYTES; ++i) old[i] = banks[i%8][i/8]; \
        write_``W = {probe_t'(old), next_fault}; \
    endfunction \
    function static logic [W+31:0] read_``W( \
        input probe_t old, input logic [63:0] address, input logic [31:0] fault, \
        input logic [7:0][(1 << $clog2(MEM_BYTES/8))-1:0][7:0] banks = '0); \
        for (int unsigned i = 0; i < MEM_BYTES; ++i) banks[i%8][i/8] = old[i]; \
        read_``W = dut.hls_storage_read_``W(banks[0], banks[1], banks[2], banks[3], \
            banks[4], banks[5], banks[6], banks[7], address, fault); \
    endfunction

`STORAGE_ADAPTER(8)
`STORAGE_ADAPTER(16)
`STORAGE_ADAPTER(24)
`STORAGE_ADAPTER(32)
`STORAGE_ADAPTER(64)
`STORAGE_ADAPTER(128)
`undef STORAGE_ADAPTER
`undef BANK_WRITE
