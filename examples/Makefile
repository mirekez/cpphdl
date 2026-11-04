# Example: make -C examples EXAMPLE=math/Maximum.h native rtl
ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
EXAMPLE ?= math/Maximum.h
HEADER_ROOT ?= $(ROOT)/examples
HEADER := $(abspath $(HEADER_ROOT)/$(EXAMPLE))
NAME := $(basename $(notdir $(HEADER)))
WORK ?= $(ROOT)/build/header-$(notdir $(HEADER_ROOT))/$(subst /,_,$(basename $(EXAMPLE)))
CXXFLAGS ?= -std=c++23 -O2 -ffp-contract=off
CPPHDL ?= $(ROOT)/build/cpphdl-static
VERILATOR ?= verilator
PYTHON ?= python3

SIMPLE := Fp32Mac Fp32Bits Fp32Add Fp32Exp Maximum SynchronousRam MaskedSynchronousRam MatrixTransfer
TOP ?= $(NAME)Example
ifeq ($(NAME),Fp32Mac)
TOP := Fp32Mac
endif
ifeq ($(NAME),MatrixTransfer)
TOP := cpphdl_examples_MatrixTransfer
endif
ifeq ($(NAME),FpAdd)
CONVERT_FLAGS := --primary_clock sum_clock 100000000
endif
ifeq ($(NAME),Fifo2clk)
CONVERT_FLAGS := --primary_clock write_clk 100000000 --secondary_clock read_clk 50000000
endif
ifneq ($(filter $(NAME),FpConverter AvalonEndpoint Axi4MuxFromSlave Axi4MuxToMaster),)
CONVERT_FLAGS := --no-synthesis-flag
endif

.PHONY: build native generate rtl
build:
	mkdir -p "$(WORK)"
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -DMAINFILE -include "$(HEADER)" -I"$(ROOT)/include" \
		"$(ROOT)/tests/ExampleMain.cpp" $(LDFLAGS) $(LDLIBS) -o "$(WORK)/test"
ifeq ($(NAME),RiscV)
	cp "$(ROOT)/examples/tuple/rv32i.bin" "$(ROOT)/examples/tuple/rv32i.log" "$(WORK)/"
endif
native: build
	cd "$(WORK)" && ./test --noveril $(ARGS)
ifneq ($(filter $(NAME),FpAdd Fifo2clk),)
# These native testbenches also verify the generated clock/edge declarations.
native: generate
endif
generate:
	mkdir -p "$(WORK)"
	"$(CPPHDL)" $(CONVERT_FLAGS) $(CPPHDL_FLAGS) --generated-dir="$(WORK)/generated" \
		"$(HEADER)" -- -DMAINFILE -I"$(ROOT)/include"
ifneq ($(filter $(NAME),$(SIMPLE)),)
rtl:
	$(PYTHON) "$(ROOT)/examples/RunRtl.py" --cpphdl "$(CPPHDL)" --cxx "$(CXX)" \
		--verilator "$(VERILATOR)" --source "$(HEADER)" --top "$(TOP)" --work "$(WORK)"
else
rtl: build generate
	cd "$(WORK)" && CPPHDL_VERILATOR="$(VERILATOR)" CPPHDL_VERILATOR_CXX="$(CXX)" ./test $(ARGS)
endif
