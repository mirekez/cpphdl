#pragma once

#include "cpphdl.h"

using namespace cpphdl;

namespace sd
{

static constexpr uint8_t CMD17_READ_SINGLE_BLOCK = 0x51;
static constexpr uint8_t CMD24_WRITE_SINGLE_BLOCK = 0x58;
static constexpr uint8_t R1_READY = 0x00;

static constexpr uint32_t DEFAULT_BLOCK_BYTES = 512;

static constexpr uint32_t REG_CONTROL = 0x00;
static constexpr uint32_t REG_STATUS = 0x04;
static constexpr uint32_t REG_CMD = 0x08;
static constexpr uint32_t REG_ARG = 0x0c;
static constexpr uint32_t REG_LEN = 0x10;
static constexpr uint32_t REG_DMA_ADDR = 0x14;
static constexpr uint32_t REG_TXDATA = 0x18;
static constexpr uint32_t REG_RXDATA = 0x1c;
static constexpr uint32_t REG_IRQ_ENABLE = 0x20;
static constexpr uint32_t REG_IRQ_PENDING = 0x24;
static constexpr uint32_t REG_DMA_DESC_ADDR = 0x28;
static constexpr uint32_t REG_DMA_DESC_LEN = 0x2c;
static constexpr uint32_t REG_DMA_DESC_PUSH = 0x30;
static constexpr uint32_t REG_DMA_DESC_STATUS = 0x34;

static constexpr uint32_t CTRL_START = 1u << 0;
static constexpr uint32_t CTRL_WRITE = 1u << 1;
static constexpr uint32_t CTRL_DMA = 1u << 2;
static constexpr uint32_t CTRL_IRQ_DONE = 1u << 3;
static constexpr uint32_t CTRL_CLEAR_DONE = 1u << 4;

static constexpr uint32_t STATUS_BUSY = 1u << 0;
static constexpr uint32_t STATUS_DONE = 1u << 1;
static constexpr uint32_t STATUS_ERROR = 1u << 2;
static constexpr uint32_t STATUS_RX_VALID = 1u << 3;
static constexpr uint32_t STATUS_TX_READY = 1u << 4;
static constexpr uint32_t STATUS_IRQ = 1u << 5;
static constexpr uint32_t STATUS_DESC_READY = 1u << 6;

static constexpr uint32_t IRQ_DONE = 1u << 0;

static constexpr uint32_t DESC_STATUS_READY = 1u << 0;
static constexpr uint32_t DESC_STATUS_EMPTY = 1u << 1;
static constexpr uint32_t DESC_STATUS_FULL = 1u << 2;
static constexpr uint32_t DESC_STATUS_COUNT_SHIFT = 8;

static constexpr uint8_t FRAME_READ = CMD17_READ_SINGLE_BLOCK;
static constexpr uint8_t FRAME_WRITE = CMD24_WRITE_SINGLE_BLOCK;

}
