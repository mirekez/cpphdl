#pragma once

#include "cpphdl.h"
#include "ethgig_pcs.h"

using namespace cpphdl;

template<size_t FIFO_DEPTH = 2048>
class EthGigMAC : public Module
{
public:
    _PORT(logic<48>) local_mac_in;
    _PORT(uint32_t) local_ip_in;
    _PORT(uint32_t) local_mask_in;
    _PORT(bool) promisc_in;

    _PORT(bool) tx_valid_in;
    _PORT(u<8>) tx_data_in;
    _PORT(bool) tx_last_in;
    _PORT(bool) tx_ready_out = _ASSIGN(tx_count_reg != FIFO_DEPTH);

    _PORT(bool) rx_valid_out = _ASSIGN(rx_count_reg != 0);
    _PORT(u<8>) rx_data_out = _ASSIGN(rx_data_reg[(uint32_t)rx_rd_reg]);
    _PORT(bool) rx_last_out = _ASSIGN(rx_last_reg[(uint32_t)rx_rd_reg]);
    _PORT(bool) rx_ready_in;

    _PORT(bool) pcs_tx_valid_out = _ASSIGN_REG(pcs_tx_valid_reg);
    _PORT(u<8>) pcs_tx_data_out = _ASSIGN_REG(pcs_tx_data_reg);
    _PORT(bool) pcs_tx_last_out = _ASSIGN_REG(pcs_tx_last_reg);
    _PORT(bool) pcs_tx_ready_in;

    _PORT(bool) pcs_rx_valid_in;
    _PORT(u<8>) pcs_rx_data_in;
    _PORT(bool) pcs_rx_last_in;
    _PORT(bool) pcs_rx_ready_out = _ASSIGN(rx_state_reg != RX_COPY && rx_frame_count_reg != FIFO_DEPTH);

    _PORT(uint32_t) tx_frames_out = _ASSIGN_REG(tx_frames_reg);
    _PORT(uint32_t) rx_frames_out = _ASSIGN_REG(rx_frames_reg);
    _PORT(uint32_t) tx_bytes_out = _ASSIGN_REG(tx_bytes_reg);
    _PORT(uint32_t) rx_bytes_out = _ASSIGN_REG(rx_bytes_reg);

private:
    static constexpr size_t INDEX_BITS = clog2(FIFO_DEPTH);
    static constexpr size_t COUNT_BITS = clog2(FIFO_DEPTH + 1);

    reg<array<FIFO_DEPTH, u<8>>> tx_data_reg;
    reg<array<FIFO_DEPTH, u1>> tx_last_reg;
    reg<u<INDEX_BITS>> tx_rd_reg;
    reg<u<INDEX_BITS>> tx_wr_reg;
    reg<u<COUNT_BITS>> tx_count_reg;

    reg<u1> pcs_tx_valid_reg;
    reg<u<8>> pcs_tx_data_reg;
    reg<u1> pcs_tx_last_reg;
    reg<u<3>> tx_state_reg;
    reg<u<4>> tx_preamble_index_reg;
    reg<u32> tx_crc_reg;
    reg<u32> tx_payload_len_reg;
    reg<u32> tx_fcs_reg;
    reg<u<3>> tx_fcs_index_reg;
    reg<u32> tx_ipg_count_reg;

    reg<array<FIFO_DEPTH, u<8>>> rx_data_reg;
    reg<array<FIFO_DEPTH, u1>> rx_last_reg;
    reg<u<INDEX_BITS>> rx_rd_reg;
    reg<u<INDEX_BITS>> rx_wr_reg;
    reg<u<COUNT_BITS>> rx_count_reg;

    reg<array<FIFO_DEPTH, u<8>>> rx_frame_data_reg;
    reg<u<COUNT_BITS>> rx_frame_count_reg;
    reg<u<COUNT_BITS>> rx_payload_count_reg;
    reg<u<COUNT_BITS>> rx_copy_index_reg;
    reg<u<4>> rx_preamble_count_reg;
    reg<u<3>> rx_state_reg;
    reg<u32> rx_crc_reg;

    reg<u32> tx_frames_reg;
    reg<u32> rx_frames_reg;
    reg<u32> tx_bytes_reg;
    reg<u32> rx_bytes_reg;

    static constexpr uint32_t TX_IDLE = 0;
    static constexpr uint32_t TX_PREAMBLE = 1;
    static constexpr uint32_t TX_PAYLOAD = 2;
    static constexpr uint32_t TX_PAD = 3;
    static constexpr uint32_t TX_FCS = 4;
    static constexpr uint32_t TX_IPG = 5;

    static constexpr uint32_t RX_SEEK = 0;
    static constexpr uint32_t RX_PAYLOAD = 1;
    static constexpr uint32_t RX_COPY = 2;

    static constexpr uint32_t ETHERNET_MIN_PAYLOAD_BYTES = 60;
    static constexpr uint32_t ETHERNET_IPG_BYTES = 12;
    static constexpr uint32_t ETHERNET_CRC_RESIDUE = 0xdebb20e3u;

    _LAZY_COMB(tx_rd_next_comb, u<INDEX_BITS>)
        if ((uint32_t)tx_rd_reg + 1u >= FIFO_DEPTH) {
            return tx_rd_next_comb = 0;
        }
        return tx_rd_next_comb = tx_rd_reg + 1;
    }

    _LAZY_COMB(tx_wr_next_comb, u<INDEX_BITS>)
        if ((uint32_t)tx_wr_reg + 1u >= FIFO_DEPTH) {
            return tx_wr_next_comb = 0;
        }
        return tx_wr_next_comb = tx_wr_reg + 1;
    }

    _LAZY_COMB(rx_rd_next_comb, u<INDEX_BITS>)
        if ((uint32_t)rx_rd_reg + 1u >= FIFO_DEPTH) {
            return rx_rd_next_comb = 0;
        }
        return rx_rd_next_comb = rx_rd_reg + 1;
    }

    _LAZY_COMB(rx_wr_next_comb, u<INDEX_BITS>)
        if ((uint32_t)rx_wr_reg + 1u >= FIFO_DEPTH) {
            return rx_wr_next_comb = 0;
        }
        return rx_wr_next_comb = rx_wr_reg + 1;
    }

    uint32_t crc32_next(uint32_t crc, uint8_t data)
    {
        uint32_t i;
        uint32_t value;
        value = crc ^ data;
        for (i = 0; i < 8; ++i) {
            if ((value & 1u) != 0u) {
                value = (value >> 1) ^ 0xedb88320u;
            }
            else {
                value = value >> 1;
            }
        }
        return value;
    }

    bool rx_dest_is_broadcast()
    {
        uint32_t i;
        bool ok;
        ok = true;
        for (i = 0; i < 6; ++i) {
            if ((uint8_t)rx_frame_data_reg[i] != 0xffu) {
                ok = false;
            }
        }
        return ok;
    }

    bool rx_dest_matches_local()
    {
        uint32_t i;
        bool ok;
        ok = true;
        for (i = 0; i < 6; ++i) {
            if ((uint8_t)rx_frame_data_reg[i] != (uint8_t)local_mac_in().bits(i * 8 + 7, i * 8)) {
                ok = false;
            }
        }
        return ok;
    }

    uint32_t rx_ipv4_dst_addr()
    {
        uint32_t value;
        value = 0;
        value |= (uint32_t)(uint8_t)rx_frame_data_reg[30] << 24;
        value |= (uint32_t)(uint8_t)rx_frame_data_reg[31] << 16;
        value |= (uint32_t)(uint8_t)rx_frame_data_reg[32] << 8;
        value |= (uint32_t)(uint8_t)rx_frame_data_reg[33];
        return value;
    }

    bool rx_frame_accept()
    {
        uint32_t ethertype;
        uint32_t mask;
        bool mac_ok;
        bool ip_ok;

        if (promisc_in()) {
            return true;
        }

        mac_ok = rx_dest_is_broadcast() || rx_dest_matches_local();
        if (!mac_ok) {
            return false;
        }

        ethertype = ((uint32_t)(uint8_t)rx_frame_data_reg[12] << 8) | (uint32_t)(uint8_t)rx_frame_data_reg[13];
        mask = local_mask_in();
        ip_ok = true;
        if (ethertype == 0x0800u && mask != 0u && (uint32_t)rx_frame_count_reg >= 38u) {
            ip_ok = (rx_ipv4_dst_addr() & mask) == (local_ip_in() & mask);
        }
        return ip_ok;
    }

public:
    void _assign() {}

    void _work(bool reset)
    {
        bool tx_push;
        bool tx_can_emit;
        bool tx_pop_payload;
        bool rx_byte_in;
        bool rx_pop;
        bool rx_fifo_push;
        bool rx_frame_done;
        uint8_t tx_byte;
        uint8_t rx_byte;
        uint32_t next_crc;
        uint32_t next_len;
        uint32_t next_count;

        tx_push = tx_valid_in() && tx_ready_out();
        tx_can_emit = !pcs_tx_valid_reg || pcs_tx_ready_in();
        tx_pop_payload = false;
        rx_byte_in = pcs_rx_valid_in() && pcs_rx_ready_out();
        rx_pop = rx_valid_out() && rx_ready_in();
        rx_fifo_push = false;

        if (tx_push) {
            tx_data_reg._next[(uint32_t)tx_wr_reg] = tx_data_in();
            tx_last_reg._next[(uint32_t)tx_wr_reg] = tx_last_in();
            tx_wr_reg._next = tx_wr_next_comb_func();
        }

        if (tx_can_emit) {
            pcs_tx_valid_reg._next = false;
            pcs_tx_last_reg._next = false;

            if (tx_state_reg == TX_IDLE) {
                if (tx_count_reg != 0) {
                    tx_state_reg._next = TX_PREAMBLE;
                    tx_preamble_index_reg._next = 0;
                    tx_crc_reg._next = 0xffffffffu;
                    tx_payload_len_reg._next = 0;
                }
            }
            else if (tx_state_reg == TX_PREAMBLE) {
                pcs_tx_valid_reg._next = true;
                if ((uint32_t)tx_preamble_index_reg < 7u) {
                    pcs_tx_data_reg._next = 0x55;
                    tx_preamble_index_reg._next = tx_preamble_index_reg + 1;
                }
                else {
                    pcs_tx_data_reg._next = 0xd5;
                    tx_state_reg._next = TX_PAYLOAD;
                }
            }
            else if (tx_state_reg == TX_PAYLOAD) {
                if (tx_count_reg != 0) {
                    tx_byte = tx_data_reg[(uint32_t)tx_rd_reg];
                    next_crc = crc32_next(tx_crc_reg, tx_byte);
                    next_len = (uint32_t)tx_payload_len_reg + 1u;
                    pcs_tx_valid_reg._next = true;
                    pcs_tx_data_reg._next = tx_byte;
                    tx_crc_reg._next = next_crc;
                    tx_payload_len_reg._next = next_len;
                    tx_pop_payload = true;
                    if (tx_last_reg[(uint32_t)tx_rd_reg]) {
                        if (next_len < ETHERNET_MIN_PAYLOAD_BYTES) {
                            tx_state_reg._next = TX_PAD;
                        }
                        else {
                            tx_fcs_reg._next = ~next_crc;
                            tx_fcs_index_reg._next = 0;
                            tx_state_reg._next = TX_FCS;
                        }
                    }
                }
            }
            else if (tx_state_reg == TX_PAD) {
                next_crc = crc32_next(tx_crc_reg, 0);
                next_len = (uint32_t)tx_payload_len_reg + 1u;
                pcs_tx_valid_reg._next = true;
                pcs_tx_data_reg._next = 0;
                tx_crc_reg._next = next_crc;
                tx_payload_len_reg._next = next_len;
                if (next_len >= ETHERNET_MIN_PAYLOAD_BYTES) {
                    tx_fcs_reg._next = ~next_crc;
                    tx_fcs_index_reg._next = 0;
                    tx_state_reg._next = TX_FCS;
                }
            }
            else if (tx_state_reg == TX_FCS) {
                pcs_tx_valid_reg._next = true;
                pcs_tx_data_reg._next = (uint8_t)(((uint32_t)tx_fcs_reg >> ((uint32_t)tx_fcs_index_reg * 8u)) & 0xffu);
                pcs_tx_last_reg._next = (uint32_t)tx_fcs_index_reg == 3u;
                if ((uint32_t)tx_fcs_index_reg == 3u) {
                    tx_state_reg._next = TX_IPG;
                    tx_ipg_count_reg._next = 0;
                    tx_frames_reg._next = tx_frames_reg + 1;
                }
                else {
                    tx_fcs_index_reg._next = tx_fcs_index_reg + 1;
                }
            }
            else if (tx_state_reg == TX_IPG) {
                if ((uint32_t)tx_ipg_count_reg + 1u >= ETHERNET_IPG_BYTES) {
                    tx_state_reg._next = TX_IDLE;
                }
                else {
                    tx_ipg_count_reg._next = tx_ipg_count_reg + 1;
                }
            }
        }
        if (tx_pop_payload) {
            tx_rd_reg._next = tx_rd_next_comb_func();
            tx_bytes_reg._next = tx_bytes_reg + 1;
        }
        if (tx_push && !tx_pop_payload) {
            tx_count_reg._next = tx_count_reg + 1;
        }
        else if (!tx_push && tx_pop_payload) {
            tx_count_reg._next = tx_count_reg - 1;
        }

        if (rx_byte_in) {
            rx_byte = pcs_rx_data_in();
            if (rx_state_reg == RX_SEEK) {
                if (rx_byte == 0x55u && (uint32_t)rx_preamble_count_reg < 7u) {
                    rx_preamble_count_reg._next = rx_preamble_count_reg + 1;
                }
                else if (rx_byte == 0xd5u && (uint32_t)rx_preamble_count_reg >= 7u) {
                    rx_state_reg._next = RX_PAYLOAD;
                    rx_frame_count_reg._next = 0;
                    rx_crc_reg._next = 0xffffffffu;
                    rx_preamble_count_reg._next = 0;
                }
                else if (rx_byte == 0x55u) {
                    rx_preamble_count_reg._next = 1;
                }
                else {
                    rx_preamble_count_reg._next = 0;
                }
            }
            else if (rx_state_reg == RX_PAYLOAD) {
                next_count = (uint32_t)rx_frame_count_reg + 1u;
                next_crc = crc32_next(rx_crc_reg, rx_byte);
                rx_frame_data_reg._next[(uint32_t)rx_frame_count_reg] = rx_byte;
                rx_frame_count_reg._next = next_count;
                rx_crc_reg._next = next_crc;
                rx_frame_done = pcs_rx_last_in();
                if (rx_frame_done) {
                    if (next_count >= 64u && next_crc == ETHERNET_CRC_RESIDUE && rx_frame_accept()) {
                        rx_payload_count_reg._next = next_count - 4u;
                        rx_copy_index_reg._next = 0;
                        rx_state_reg._next = RX_COPY;
                    }
                    else {
                        rx_state_reg._next = RX_SEEK;
                        rx_frame_count_reg._next = 0;
                        rx_preamble_count_reg._next = 0;
                    }
                }
            }
        }

        if (rx_state_reg == RX_COPY && rx_count_reg != FIFO_DEPTH) {
            rx_fifo_push = true;
            rx_data_reg._next[(uint32_t)rx_wr_reg] = rx_frame_data_reg[(uint32_t)rx_copy_index_reg];
            rx_last_reg._next[(uint32_t)rx_wr_reg] = (uint32_t)rx_copy_index_reg + 1u >= (uint32_t)rx_payload_count_reg;
            rx_wr_reg._next = rx_wr_next_comb_func();
            rx_bytes_reg._next = rx_bytes_reg + 1;
            if ((uint32_t)rx_copy_index_reg + 1u >= (uint32_t)rx_payload_count_reg) {
                rx_frames_reg._next = rx_frames_reg + 1;
                rx_state_reg._next = RX_SEEK;
                rx_frame_count_reg._next = 0;
                rx_copy_index_reg._next = 0;
            }
            else {
                rx_copy_index_reg._next = rx_copy_index_reg + 1;
            }
        }
        if (rx_pop) {
            rx_rd_reg._next = rx_rd_next_comb_func();
        }
        if (rx_fifo_push && !rx_pop) {
            rx_count_reg._next = rx_count_reg + 1;
        }
        else if (!rx_fifo_push && rx_pop) {
            rx_count_reg._next = rx_count_reg - 1;
        }

        if (reset) {
            tx_rd_reg._next = 0;
            tx_wr_reg._next = 0;
            tx_count_reg._next = 0;
            pcs_tx_valid_reg._next = false;
            pcs_tx_data_reg._next = 0;
            pcs_tx_last_reg._next = false;
            tx_state_reg._next = TX_IDLE;
            tx_preamble_index_reg._next = 0;
            tx_crc_reg._next = 0xffffffffu;
            tx_payload_len_reg._next = 0;
            tx_fcs_reg._next = 0;
            tx_fcs_index_reg._next = 0;
            tx_ipg_count_reg._next = 0;
            rx_rd_reg._next = 0;
            rx_wr_reg._next = 0;
            rx_count_reg._next = 0;
            rx_frame_count_reg._next = 0;
            rx_payload_count_reg._next = 0;
            rx_copy_index_reg._next = 0;
            rx_preamble_count_reg._next = 0;
            rx_state_reg._next = RX_SEEK;
            rx_crc_reg._next = 0xffffffffu;
            tx_frames_reg._next = 0;
            rx_frames_reg._next = 0;
            tx_bytes_reg._next = 0;
            rx_bytes_reg._next = 0;
        }
    }

    void _strobe()
    {
        tx_data_reg.strobe();
        tx_last_reg.strobe();
        tx_rd_reg.strobe();
        tx_wr_reg.strobe();
        tx_count_reg.strobe();
        pcs_tx_valid_reg.strobe();
        pcs_tx_data_reg.strobe();
        pcs_tx_last_reg.strobe();
        tx_state_reg.strobe();
        tx_preamble_index_reg.strobe();
        tx_crc_reg.strobe();
        tx_payload_len_reg.strobe();
        tx_fcs_reg.strobe();
        tx_fcs_index_reg.strobe();
        tx_ipg_count_reg.strobe();
        rx_data_reg.strobe();
        rx_last_reg.strobe();
        rx_rd_reg.strobe();
        rx_wr_reg.strobe();
        rx_count_reg.strobe();
        rx_frame_data_reg.strobe();
        rx_frame_count_reg.strobe();
        rx_payload_count_reg.strobe();
        rx_copy_index_reg.strobe();
        rx_preamble_count_reg.strobe();
        rx_state_reg.strobe();
        rx_crc_reg.strobe();
        tx_frames_reg.strobe();
        rx_frames_reg.strobe();
        tx_bytes_reg.strobe();
        rx_bytes_reg.strobe();
    }
};
