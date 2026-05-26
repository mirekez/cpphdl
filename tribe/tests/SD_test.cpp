#include "cpphdl.h"
#include "Axi4.h"
#include "Axi4Ram.h"
#include "devices/sd/SDController.h"
#include "verif/SDCardVerif.h"

static constexpr size_t SD_TEST_ADDR_WIDTH = 20;
static constexpr size_t SD_TEST_DATA_WIDTH = 64;
static constexpr size_t SD_TEST_RAM_WORDS = 32768;
static constexpr size_t SD_TEST_DATA_BYTES = SD_TEST_DATA_WIDTH / 8;

#ifdef SYNTHESIS
class SDControllerTestDut : public Module
{
public:
    Axi4If<SD_TEST_ADDR_WIDTH, 4, SD_TEST_DATA_WIDTH> axi_in;
    Axi4If<SD_TEST_ADDR_WIDTH, 4, SD_TEST_DATA_WIDTH> dma_out;

    _PORT(bool) sd_cmd_valid_out;
    _PORT(u<8>) sd_cmd_data_out;
    _PORT(bool) sd_cmd_last_out;
    _PORT(bool) sd_cmd_ready_in;
    _PORT(bool) sd_rsp_valid_in;
    _PORT(u<8>) sd_rsp_data_in;
    _PORT(bool) sd_rsp_last_in;
    _PORT(bool) sd_rsp_ready_out;
    _PORT(bool) irq_out;

private:
    SDController<SD_TEST_ADDR_WIDTH, 4, SD_TEST_DATA_WIDTH, 64> dut;

public:
    void _assign()
    {
        AXI4_DRIVER_FROM(dut.axi_in, axi_in);
        AXI4_RESPONDER_FROM(axi_in, dut.axi_in);
        AXI4_DRIVER_FROM(dma_out, dut.dma_out);
        AXI4_RESPONDER_FROM(dut.dma_out, dma_out);

        dut.sd_cmd_ready_in = sd_cmd_ready_in;
        dut.sd_rsp_valid_in = sd_rsp_valid_in;
        dut.sd_rsp_data_in = sd_rsp_data_in;
        dut.sd_rsp_last_in = sd_rsp_last_in;
        sd_cmd_valid_out = dut.sd_cmd_valid_out;
        sd_cmd_data_out = dut.sd_cmd_data_out;
        sd_cmd_last_out = dut.sd_cmd_last_out;
        sd_rsp_ready_out = dut.sd_rsp_ready_out;
        irq_out = dut.irq_out;
        dut._assign();
    }

    void _work(bool reset)
    {
        dut._work(reset);
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        dut._strobe(checkpoint_fd);
    }
};
#endif

#if !defined(SYNTHESIS)

#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <print>
#include <string>
#include <vector>
#include "../../examples/tools.h"

#ifdef VERILATOR
#define MAKE_HEADER(name) STRINGIFY(name.h)
#include MAKE_HEADER(VERILATOR_MODEL)
#endif

long sys_clock = -1;

template<size_t WIDTH>
static logic<WIDTH> verilator_u32_to_logic(uint32_t value)
{
    return (logic<WIDTH>)value;
}

template<size_t WIDTH>
static uint32_t low32(logic<WIDTH> value)
{
    return (uint32_t)value.bits(31, 0);
}

class SDDirectTest : public Module
{
#ifdef VERILATOR
    VERILATOR_MODEL dut;
#else
    SDController<SD_TEST_ADDR_WIDTH, 4, SD_TEST_DATA_WIDTH, 64> dut;
#endif
    Axi4Ram<SD_TEST_ADDR_WIDTH, 4, SD_TEST_DATA_WIDTH, SD_TEST_RAM_WORDS> ram;
    SDCardVerifFrontend card;
    Axi4Driver<SD_TEST_ADDR_WIDTH, 4, SD_TEST_DATA_WIDTH> ctrl = {};
#ifdef VERILATOR
    Axi4Driver<SD_TEST_ADDR_WIDTH, 4, SD_TEST_DATA_WIDTH> dma_drv = {};
#endif
    bool error = false;
    uint32_t dma_complete_pulses = 0;

    void fail(const char* text)
    {
        std::print("\n{}\n", text);
        error = true;
    }

public:
    SDDirectTest()
        : card(1 << 20)
    {
        card.fill_prbs();
    }

    void _assign()
    {
#ifndef VERILATOR
        AXI4_DRIVER_FROM_DRIVER(dut.axi_in, ctrl);
        dut.sd_cmd_ready_in = card.sd_cmd_ready_out;
        dut.sd_rsp_valid_in = card.sd_rsp_valid_out;
        dut.sd_rsp_data_in = card.sd_rsp_data_out;
        dut.sd_rsp_last_in = card.sd_rsp_last_out;
        dut.__inst_name = "sd";
        dut._assign();
        card.sd_cmd_valid_in = dut.sd_cmd_valid_out;
        card.sd_cmd_data_in = dut.sd_cmd_data_out;
        card.sd_cmd_last_in = dut.sd_cmd_last_out;
        card.sd_rsp_ready_in = dut.sd_rsp_ready_out;
        card.__inst_name = "sd_card";
        card._assign();
        AXI4_DRIVER_FROM(ram.axi_in, dut.dma_out);
        ram.debugen_in = false;
        ram.__inst_name = "dma_ram";
        ram._assign();
        AXI4_RESPONDER_FROM(dut.dma_out, ram.axi_in);
#else
        ram.axi_in = dma_drv;
        ram.debugen_in = false;
        ram.__inst_name = "dma_ram";
        ram._assign();
        card.sd_cmd_valid_in = _ASSIGN((bool)dut.sd_cmd_valid_out);
        card.sd_cmd_data_in = _ASSIGN((u<8>)(uint8_t)dut.sd_cmd_data_out);
        card.sd_cmd_last_in = _ASSIGN((bool)dut.sd_cmd_last_out);
        card.sd_rsp_ready_in = _ASSIGN((bool)dut.sd_rsp_ready_out);
        card.__inst_name = "sd_card";
        card._assign();
#endif
    }

#ifdef VERILATOR
    void poke_ctrl()
    {
        AXI4_DRIVER_POKE_VERILATOR_IF_FROM_DRIVER(dut, axi_in, ctrl);
        dut.sd_cmd_ready_in = card.sd_cmd_ready_out();
        dut.sd_rsp_valid_in = card.sd_rsp_valid_out();
        dut.sd_rsp_data_in = (uint8_t)card.sd_rsp_data_out();
        dut.sd_rsp_last_in = card.sd_rsp_last_out();
        dut.dma_out___05Fawready_in = ram.axi_in.awready_out();
        dut.dma_out___05Fwready_in = ram.axi_in.wready_out();
        dut.dma_out___05Fbvalid_in = ram.axi_in.bvalid_out();
        dut.dma_out___05Fbid_in = (uint32_t)ram.axi_in.bid_out();
        dut.dma_out___05Farready_in = ram.axi_in.arready_out();
        dut.dma_out___05Frvalid_in = ram.axi_in.rvalid_out();
        dut.dma_out___05Frdata_in = (uint64_t)ram.axi_in.rdata_out();
        dut.dma_out___05Frlast_in = ram.axi_in.rlast_out();
        dut.dma_out___05Frid_in = (uint32_t)ram.axi_in.rid_out();
    }

    void sample_dma_driver()
    {
        dma_drv.aw.valid = dut.dma_out___05Fawvalid_out;
        dma_drv.aw.addr = (u<SD_TEST_ADDR_WIDTH>)(uint32_t)dut.dma_out___05Fawaddr_out;
        dma_drv.aw.id = (u<4>)(uint32_t)dut.dma_out___05Fawid_out;
        dma_drv.w.valid = dut.dma_out___05Fwvalid_out;
        dma_drv.w.data = (logic<SD_TEST_DATA_WIDTH>)(uint64_t)dut.dma_out___05Fwdata_out;
        dma_drv.w.last = dut.dma_out___05Fwlast_out;
        dma_drv.b.ready = dut.dma_out___05Fbready_out;
        dma_drv.ar.valid = dut.dma_out___05Farvalid_out;
        dma_drv.ar.addr = (u<SD_TEST_ADDR_WIDTH>)(uint32_t)dut.dma_out___05Faraddr_out;
        dma_drv.ar.id = (u<4>)(uint32_t)dut.dma_out___05Farid_out;
        dma_drv.r.ready = dut.dma_out___05Frready_out;
    }
#endif

    void cycle(bool reset = false)
    {
#ifdef VERILATOR
        poke_ctrl();
        dut.clk = 0;
        dut.reset = reset;
        dut.eval();
        sample_dma_driver();
        ram._work(reset);
        poke_ctrl();
        dut.clk = 1;
        dut.reset = reset;
        dut.eval();
        sample_dma_driver();
        ram._strobe();
        card._work(reset);
        card._strobe();
        poke_ctrl();
        dut.clk = 0;
        dut.reset = reset;
        dut.eval();
#else
        dut._work(reset);
        ram._work(reset);
        dut._strobe();
        ram._strobe();
        card._work(reset);
        card._strobe();
#endif
        if (dma_write_complete()) {
            ++dma_complete_pulses;
        }
        ++sys_clock;
    }

    void reset()
    {
        for (int i = 0; i < 4; ++i) {
            cycle(true);
        }
    }

    void write32(uint32_t addr, uint32_t data)
    {
        ctrl.aw.valid = true;
        ctrl.aw.addr = (u<SD_TEST_ADDR_WIDTH>)addr;
        ctrl.aw.id = 1;
        ctrl.w.valid = false;
        ctrl.b.ready = true;
        cycle();
        ctrl.aw.valid = false;
        ctrl.w.valid = true;
        ctrl.w.data = (logic<SD_TEST_DATA_WIDTH>)data;
        ctrl.w.last = true;
        cycle();
        ctrl.w.valid = false;
        for (int i = 0; i < 8 && !ctrl.b.ready; ++i) {
            cycle();
        }
        cycle();
        ctrl.b.ready = false;
    }

    uint32_t read32(uint32_t addr)
    {
        uint32_t value;
        ctrl.ar.valid = true;
        ctrl.ar.addr = (u<SD_TEST_ADDR_WIDTH>)addr;
        ctrl.ar.id = 2;
        ctrl.r.ready = false;
        cycle();
        ctrl.ar.valid = false;
        for (int i = 0; i < 16; ++i) {
#ifdef VERILATOR
            if (dut.axi_in___05Frvalid_out) {
                value = (uint32_t)dut.axi_in___05Frdata_out;
                ctrl.r.ready = true;
                cycle();
                ctrl.r.ready = false;
                return value;
            }
#else
            if (dut.axi_in.rvalid_out()) {
                value = low32(dut.axi_in.rdata_out());
                ctrl.r.ready = true;
                cycle();
                ctrl.r.ready = false;
                return value;
            }
#endif
            cycle();
        }
        fail("SD AXI read timeout");
        return 0;
    }

    uint32_t ram_read_byte(uint32_t addr)
    {
        return (uint8_t)ram.ram.buffer.data[addr / SD_TEST_DATA_BYTES][addr % SD_TEST_DATA_BYTES];
    }

    bool dma_write_complete()
    {
#ifdef VERILATOR
        return dut.dma_write_complete_out;
#else
        return dut.dma_write_complete_out();
#endif
    }

    void ram_write_byte(uint32_t addr, uint32_t value)
    {
        ram.ram.buffer.data[addr / SD_TEST_DATA_BYTES][addr % SD_TEST_DATA_BYTES] = (uint8_t)value;
    }

    void push_desc(uint32_t addr, uint32_t len)
    {
        write32(sd::REG_DMA_DESC_ADDR, addr);
        write32(sd::REG_DMA_DESC_LEN, len);
        write32(sd::REG_DMA_DESC_PUSH, 1);
    }

    void mmio_lane_check()
    {
        // Regression for the Linux path: status and RXDATA live at non-zero
        // word offsets in a 64-bit AXI beat, so reads must not return only
        // lane 0 data.
        write32(sd::REG_CONTROL, sd::CTRL_CLEAR_DONE);
        write32(sd::REG_CMD, sd::CMD17_READ_SINGLE_BLOCK);
        write32(sd::REG_ARG, 1);
        write32(sd::REG_LEN, 1);
        write32(sd::REG_CONTROL, sd::CTRL_START);
        for (int i = 0; i < 2000; ++i) {
            if (read32(sd::REG_STATUS) & sd::STATUS_RX_VALID) {
                uint32_t got = read32(sd::REG_RXDATA) & 0xffu;
                uint32_t expected = card.read_byte(sd::DEFAULT_BLOCK_BYTES);
                if (got != expected) {
                    std::print("\nMMIO lane read mismatch got={:02x} expected={:02x}\n", got, expected);
                    error = true;
                }
                wait_done();
                return;
            }
            cycle();
        }
        fail("MMIO lane status read timeout");
    }

    bool wait_done()
    {
        for (int i = 0; i < 1000000; ++i) {
            uint32_t status = read32(sd::REG_STATUS);
            if (status & sd::STATUS_ERROR) {
                fail("SD status error");
                return false;
            }
            if (status & sd::STATUS_DONE) {
                return true;
            }
            cycle();
        }
        fail("SD operation timeout");
        return false;
    }

    void pio_read_check(uint32_t block, uint32_t len)
    {
        write32(sd::REG_CONTROL, sd::CTRL_CLEAR_DONE);
        write32(sd::REG_CMD, sd::CMD17_READ_SINGLE_BLOCK);
        write32(sd::REG_ARG, block);
        write32(sd::REG_LEN, len);
        write32(sd::REG_CONTROL, sd::CTRL_START);
        for (uint32_t i = 0; i < len; ++i) {
            for (int wait = 0; wait < 2000; ++wait) {
                if (read32(sd::REG_STATUS) & sd::STATUS_RX_VALID) {
                    break;
                }
                cycle();
            }
            uint32_t got = read32(sd::REG_RXDATA) & 0xffu;
            uint32_t expected = card.read_byte(block * sd::DEFAULT_BLOCK_BYTES + i);
            if (got != expected) {
                std::print("\nPIO read mismatch block={} pos={} got={:02x} expected={:02x} card_cmd={:02x} card_block={} card_len={} card_first={:02x}\n",
                    block, i, got, expected, card.last_cmd(), card.last_block(), card.last_len(), card.last_first_data());
                error = true;
                return;
            }
        }
        wait_done();
    }

    void pio_write_readback(uint32_t block, uint32_t len, uint32_t seed)
    {
        write32(sd::REG_CONTROL, sd::CTRL_CLEAR_DONE);
        write32(sd::REG_CMD, sd::CMD24_WRITE_SINGLE_BLOCK);
        write32(sd::REG_ARG, block);
        write32(sd::REG_LEN, len);
        for (uint32_t i = 0; i < len; ++i) {
            write32(sd::REG_TXDATA, (seed + i * 17u) & 0xffu);
        }
        write32(sd::REG_CONTROL, sd::CTRL_START | sd::CTRL_WRITE);
        wait_done();
        for (uint32_t i = 0; i < len; ++i) {
            uint32_t expected = (seed + i * 17u) & 0xffu;
            uint32_t got = card.read_byte(block * sd::DEFAULT_BLOCK_BYTES + i);
            if (got != expected) {
                std::print("\nPIO write mismatch block={} pos={} got={:02x} expected={:02x}\n", block, i, got, expected);
                error = true;
                return;
            }
        }
    }

    void dma_read_check(uint32_t block, uint32_t len, uint32_t addr)
    {
        write32(sd::REG_CONTROL, sd::CTRL_CLEAR_DONE);
        write32(sd::REG_CMD, sd::CMD17_READ_SINGLE_BLOCK);
        write32(sd::REG_ARG, block);
        write32(sd::REG_LEN, len);
        write32(sd::REG_DMA_ADDR, addr);
        write32(sd::REG_CONTROL, sd::CTRL_START | sd::CTRL_DMA);
        wait_done();
        for (uint32_t i = 0; i < len; ++i) {
            uint32_t got = ram_read_byte(addr + i);
            uint32_t expected = card.read_byte(block * sd::DEFAULT_BLOCK_BYTES + i);
            if (got != expected) {
                std::print("\nDMA read mismatch block={} pos={} got={:02x} expected={:02x}\n", block, i, got, expected);
                error = true;
                return;
            }
        }
    }

    void dma_write_readback(uint32_t block, uint32_t len, uint32_t addr, uint32_t seed)
    {
        for (uint32_t base = 0; base < len; base += SD_TEST_DATA_BYTES) {
            logic<SD_TEST_DATA_WIDTH> word = 0;
            for (uint32_t lane = 0; lane < SD_TEST_DATA_BYTES && base + lane < len; ++lane) {
                word.bits(lane * 8 + 7, lane * 8) = (uint8_t)((seed + base + lane) & 0xffu);
            }
            ram.ram.buffer[(addr + base) / SD_TEST_DATA_BYTES] = word;
        }
        write32(sd::REG_CONTROL, sd::CTRL_CLEAR_DONE);
        write32(sd::REG_CMD, sd::CMD24_WRITE_SINGLE_BLOCK);
        write32(sd::REG_ARG, block);
        write32(sd::REG_LEN, len);
        write32(sd::REG_DMA_ADDR, addr);
        write32(sd::REG_CONTROL, sd::CTRL_START | sd::CTRL_WRITE | sd::CTRL_DMA);
        wait_done();
        for (uint32_t i = 0; i < len; ++i) {
            uint32_t expected = (seed + i) & 0xffu;
            uint32_t got = card.read_byte(block * sd::DEFAULT_BLOCK_BYTES + i);
            if (got != expected) {
                std::print("\nDMA write mismatch block={} pos={} got={:02x} expected={:02x}\n", block, i, got, expected);
                error = true;
                return;
            }
        }
    }

    void dma_desc_read_check(uint32_t block, const std::vector<std::pair<uint32_t, uint32_t>>& descs, bool check_completion_pulse = false)
    {
        uint32_t total = 0;
        uint32_t pos = 0;
        for (const auto& desc : descs) {
            total += desc.second;
        }
        write32(sd::REG_CONTROL, sd::CTRL_CLEAR_DONE);
        write32(sd::REG_CMD, sd::CMD17_READ_SINGLE_BLOCK);
        write32(sd::REG_ARG, block);
        write32(sd::REG_LEN, total);
        for (const auto& desc : descs) {
            push_desc(desc.first, desc.second);
        }
        dma_complete_pulses = 0;
        write32(sd::REG_CONTROL, sd::CTRL_START | sd::CTRL_DMA);
        for (int i = 0; i < 1000000; ++i) {
            uint32_t status = read32(sd::REG_STATUS);
            if (status & sd::STATUS_ERROR) {
                fail("SD status error");
                return;
            }
            if (status & sd::STATUS_DONE) {
                break;
            }
            cycle();
            if (i == 999999) {
                fail("SD operation timeout");
                return;
            }
        }
        if (check_completion_pulse && dma_complete_pulses != 1) {
            std::print("\nDMA descriptor read completion pulse count mismatch: got={} expected=1\n", dma_complete_pulses);
            error = true;
            return;
        }
        for (const auto& desc : descs) {
            for (uint32_t i = 0; i < desc.second; ++i) {
                uint32_t got = ram_read_byte(desc.first + i);
                uint32_t expected = card.read_byte(block * sd::DEFAULT_BLOCK_BYTES + pos);
                if (got != expected) {
                    std::print("\nDMA descriptor read mismatch block={} pos={} addr={:x} got={:02x} expected={:02x}\n",
                        block, pos, desc.first + i, got, expected);
                    error = true;
                    return;
                }
                ++pos;
            }
        }
    }

    void dma_desc_write_readback(uint32_t block, const std::vector<std::pair<uint32_t, uint32_t>>& descs, uint32_t seed)
    {
        uint32_t total = 0;
        uint32_t pos = 0;
        for (const auto& desc : descs) {
            total += desc.second;
            for (uint32_t i = 0; i < desc.second; ++i) {
                ram_write_byte(desc.first + i, (seed + pos * 5u) & 0xffu);
                ++pos;
            }
        }
        ram.ram.buffer.apply();
        write32(sd::REG_CONTROL, sd::CTRL_CLEAR_DONE);
        write32(sd::REG_CMD, sd::CMD24_WRITE_SINGLE_BLOCK);
        write32(sd::REG_ARG, block);
        write32(sd::REG_LEN, total);
        for (const auto& desc : descs) {
            push_desc(desc.first, desc.second);
        }
        write32(sd::REG_CONTROL, sd::CTRL_START | sd::CTRL_WRITE | sd::CTRL_DMA);
        wait_done();
        pos = 0;
        for (const auto& desc : descs) {
            for (uint32_t i = 0; i < desc.second; ++i) {
                uint32_t got = card.read_byte(block * sd::DEFAULT_BLOCK_BYTES + pos);
                uint32_t expected = (seed + pos * 5u) & 0xffu;
                if (got != expected) {
                    std::print("\nDMA descriptor write mismatch block={} pos={} addr={:x} got={:02x} expected={:02x}\n",
                        block, pos, desc.first + i, got, expected);
                    error = true;
                    return;
                }
                ++pos;
            }
        }
    }

    bool run()
    {
#ifdef VERILATOR
        std::print("VERILATOR TestSD...");
#else
        std::print("CppHDL TestSD...");
#endif
        auto start = std::chrono::high_resolution_clock::now();
        __inst_name = "sd_test";
        _assign();
        reset();

        mmio_lane_check();
        pio_read_check(2, 32);
        pio_write_readback(5, 32, 0x40);
        dma_read_check(7, 64, 0x100);
        dma_write_readback(9, 64, 0x200, 0xa0);
        dma_desc_read_check(11, {{0x300, 13}, {0x440, 29}, {0x580, 22}});
        dma_desc_write_readback(13, {{0x680, 17}, {0x7c0, 31}, {0x900, 16}}, 0x55);
        dma_desc_read_check(15, {{0x1000, 512}, {0x2000, 512}}, true);
        dma_desc_write_readback(18, {{0x3000, 512}, {0x4000, 512}}, 0xb0);
        dma_desc_read_check(40, {
            {0x08000, 4096}, {0x09000, 4096}, {0x0a000, 4096}, {0x0b000, 4096},
            {0x0c000, 4096}, {0x0d000, 4096}, {0x0e000, 4096}, {0x0f000, 4096},
            {0x10000, 4096}, {0x11000, 4096}, {0x12000, 4096}, {0x13000, 4096},
            {0x14000, 4096}, {0x15000, 4096}, {0x16000, 4096}, {0x17000, 4096},
            {0x18000, 2560},
        });
        dma_desc_write_readback(180, {
            {0x19000, 4096}, {0x1a000, 4096}, {0x1b000, 4096}, {0x1c000, 4096},
            {0x1d000, 4096}, {0x1e000, 4096}, {0x1f000, 4096}, {0x20000, 4096},
            {0x21000, 4096}, {0x22000, 4096}, {0x23000, 4096}, {0x24000, 4096},
            {0x25000, 4096}, {0x26000, 4096}, {0x27000, 4096}, {0x28000, 4096},
            {0x29000, 2560},
        }, 0x7c);

        for (uint32_t i = 0; i < 8 && !error; ++i) {
            uint32_t block = 20 + i * 3;
            uint32_t addr = 0x400 + i * 0x80;
            dma_write_readback(block, 32, addr, 0x31 + i * 9);
            dma_read_check(block, 32, addr + 0x40);
        }

        std::print(" {} ({} us)\n", !error ? "PASSED" : "FAILED",
            (std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::high_resolution_clock::now() - start)).count());
        return !error;
    }
};

int main(int argc, char** argv)
{
    bool noveril = false;
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--noveril") == 0) {
            noveril = true;
        }
    }

    bool ok = true;
#ifndef VERILATOR
    ok = SDDirectTest().run();
    if (ok && !noveril) {
        const auto source_root = CpphdlSourceRootFrom(__FILE__);
        std::print("Building SD Verilator simulation...\n");
        ok &= VerilatorCompileInExactFolder(__FILE__, "SD", "SDController",
            {"Predef_pkg"},
            {(source_root / "include").string(),
             (source_root / "tribe").string(),
             (source_root / "tribe" / "common").string(),
             (source_root / "tribe" / "devices").string(),
             (source_root / "tribe" / "verif").string()},
            SD_TEST_ADDR_WIDTH, 4, SD_TEST_DATA_WIDTH, 64);
        ok &= std::system("SD/obj_dir/VSDController") == 0;
    }
#else
    Verilated::commandArgs(argc, argv);
    ok = SDDirectTest().run();
#endif
    return ok ? 0 : 1;
}

#endif
