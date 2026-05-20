#pragma once

#include <cstdint>
#include <cstdio>
#include <deque>
#include <string>
#include <vector>

#include "cpphdl.h"
#include "sd/SDTypes.h"

using namespace cpphdl;

class SDCardVerif
{
    static constexpr uint32_t HEADER_BYTES = 7;

    std::vector<uint8_t> storage;
    std::deque<uint8_t> rsp;
    uint8_t header[HEADER_BYTES] = {};
    uint32_t header_pos = 0;
    bool write_active = false;
    uint32_t write_base = 0;
    uint32_t write_len = 0;
    uint32_t write_pos = 0;

public:
    bool cmd_ready = true;
    bool rsp_valid = false;
    uint8_t rsp_data = 0;
    bool rsp_last = false;
    uint8_t last_cmd = 0;
    uint32_t last_block = 0;
    uint32_t last_len = 0;
    uint8_t last_first_data = 0;

    explicit SDCardVerif(size_t bytes = 1 << 20)
        : storage(bytes, 0)
    {
    }

    void fill_prbs()
    {
        uint32_t x = 0x12345678u;
        for (size_t i = 0; i < storage.size(); ++i) {
            x ^= x << 13;
            x ^= x >> 17;
            x ^= x << 5;
            storage[i] = (uint8_t)(x + i);
        }
    }

    bool load_image(const std::string& path)
    {
        FILE* file = fopen(path.c_str(), "rb");
        if (!file) {
            return false;
        }
        if (fseek(file, 0, SEEK_END) != 0) {
            fclose(file);
            return false;
        }
        long size = ftell(file);
        if (size < 0) {
            fclose(file);
            return false;
        }
        if (fseek(file, 0, SEEK_SET) != 0) {
            fclose(file);
            return false;
        }
        storage.assign((size_t)size, 0);
        if (!storage.empty() && fread(storage.data(), 1, storage.size(), file) != storage.size()) {
            fclose(file);
            return false;
        }
        fclose(file);
        return true;
    }

    bool save_image(const std::string& path) const
    {
        FILE* file = fopen(path.c_str(), "wb");
        if (!file) {
            return false;
        }
        bool ok = storage.empty() || fwrite(storage.data(), 1, storage.size(), file) == storage.size();
        ok = fclose(file) == 0 && ok;
        return ok;
    }

    size_t image_size() const
    {
        return storage.size();
    }

    uint8_t read_byte(uint32_t addr) const
    {
        return addr < storage.size() ? storage[addr] : 0xffu;
    }

    void write_byte(uint32_t addr, uint8_t value)
    {
        if (addr < storage.size()) {
            storage[addr] = value;
        }
    }

    void checkpoint(FILE* checkpoint_fd)
    {
        if (!checkpoint_fd) {
            return;
        }

        uint32_t storage_size = (uint32_t)storage.size();
        checkpoint_value(checkpoint_fd, storage_size);
        if (checkpoint_reading(checkpoint_fd)) {
            storage.resize(storage_size);
            if (storage_size) {
                checkpoint_read_exact(checkpoint_fd, storage.data(), storage_size);
            }
        }
        else if (storage_size) {
            checkpoint_write_exact(checkpoint_fd, storage.data(), storage_size);
        }

        checkpoint_value(checkpoint_fd, header);
        checkpoint_value(checkpoint_fd, header_pos);
        checkpoint_value(checkpoint_fd, write_active);
        checkpoint_value(checkpoint_fd, write_base);
        checkpoint_value(checkpoint_fd, write_len);
        checkpoint_value(checkpoint_fd, write_pos);
        checkpoint_value(checkpoint_fd, cmd_ready);
        checkpoint_value(checkpoint_fd, rsp_valid);
        checkpoint_value(checkpoint_fd, rsp_data);
        checkpoint_value(checkpoint_fd, rsp_last);
        checkpoint_value(checkpoint_fd, last_cmd);
        checkpoint_value(checkpoint_fd, last_block);
        checkpoint_value(checkpoint_fd, last_len);
        checkpoint_value(checkpoint_fd, last_first_data);

        uint32_t rsp_size = (uint32_t)rsp.size();
        checkpoint_value(checkpoint_fd, rsp_size);
        if (checkpoint_reading(checkpoint_fd)) {
            rsp.clear();
            for (uint32_t i = 0; i < rsp_size; ++i) {
                uint8_t byte = 0;
                checkpoint_value(checkpoint_fd, byte);
                rsp.push_back(byte);
            }
        }
        else {
            for (uint8_t byte: rsp) {
                checkpoint_value(checkpoint_fd, byte);
            }
        }
    }

    void step(bool cmd_valid, uint8_t cmd_data, bool cmd_last, bool rsp_ready)
    {
        (void)cmd_last;
        cmd_ready = true;

        if (rsp_valid && rsp_ready) {
            rsp.pop_front();
            rsp_valid = false;
        }

        if (cmd_valid && cmd_ready) {
            if (write_active) {
                write_byte(write_base + write_pos, cmd_data);
                ++write_pos;
                if (write_pos >= write_len) {
                    write_active = false;
                    rsp.push_back(sd::R1_READY);
                }
            }
            else {
                header[header_pos++] = cmd_data;
                if (header_pos == HEADER_BYTES) {
                    const uint8_t cmd = header[0];
                    const uint32_t block =
                        (uint32_t)header[1] |
                        ((uint32_t)header[2] << 8) |
                        ((uint32_t)header[3] << 16) |
                        ((uint32_t)header[4] << 24);
                    const uint32_t len = (uint32_t)header[5] | ((uint32_t)header[6] << 8);
                    const uint32_t base = block * sd::DEFAULT_BLOCK_BYTES;
                    last_cmd = cmd;
                    last_block = block;
                    last_len = len;
                    header_pos = 0;
                    if (cmd == sd::CMD17_READ_SINGLE_BLOCK) {
                        last_first_data = read_byte(base);
                        for (uint32_t i = 0; i < len; ++i) {
                            rsp.push_back(read_byte(base + i));
                        }
                    }
                    else if (cmd == sd::CMD24_WRITE_SINGLE_BLOCK) {
                        write_active = true;
                        write_base = base;
                        write_len = len;
                        write_pos = 0;
                    }
                    else {
                        rsp.push_back(0x04);
                    }
                }
            }
        }

        if (!rsp_valid && !rsp.empty()) {
            rsp_valid = true;
            rsp_data = rsp.front();
            rsp_last = rsp.size() == 1;
        }
    }
};

class SDCardVerifFrontend : public Module
{
public:
    _PORT(bool) sd_cmd_valid_in;
    _PORT(u<8>) sd_cmd_data_in;
    _PORT(bool) sd_cmd_last_in;
    _PORT(bool) sd_cmd_ready_out = _ASSIGN(card.cmd_ready);

    _PORT(bool) sd_rsp_valid_out = _ASSIGN(card.rsp_valid);
    _PORT(u<8>) sd_rsp_data_out = _ASSIGN((u<8>)card.rsp_data);
    _PORT(bool) sd_rsp_last_out = _ASSIGN(card.rsp_last);
    _PORT(bool) sd_rsp_ready_in;

    explicit SDCardVerifFrontend(size_t bytes = 1 << 20)
        : card(bytes)
    {
    }

    void fill_prbs()
    {
        card.fill_prbs();
    }

    bool load_image(const std::string& path)
    {
        return card.load_image(path);
    }

    bool save_image(const std::string& path) const
    {
        return card.save_image(path);
    }

    size_t image_size() const
    {
        return card.image_size();
    }

    uint8_t read_byte(uint32_t addr) const
    {
        return card.read_byte(addr);
    }

    uint8_t last_cmd() const
    {
        return card.last_cmd;
    }

    uint32_t last_block() const
    {
        return card.last_block;
    }

    uint32_t last_len() const
    {
        return card.last_len;
    }

    uint8_t last_first_data() const
    {
        return card.last_first_data;
    }

    void _work(bool reset)
    {
        if (reset) {
            return;
        }
        card.step(sd_cmd_valid_in(), (uint8_t)sd_cmd_data_in(), sd_cmd_last_in(), sd_rsp_ready_in());
    }

    void _strobe(FILE* checkpoint_fd = nullptr)
    {
        card.checkpoint(checkpoint_fd);
    }

private:
    SDCardVerif card;
};
