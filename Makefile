# Build & run the ib_transport_protocol testbench as a plain C++ program.
#
#   make run                     build + run, output saved in log/
#   make run XILINX_HLS=<path>   if Vitis settings64.sh is not sourced
#   make clean
#
# Expected layout (this Makefile lives in TB_ib_transport_protocol/):
#   Coyote/
#   TB_ib_transport_protocol/
#       include/config/rocev2_config.hpp
#       include/tb/      testbench headers
#       src/             testbench sources (NOT the module's sources, those come from Coyote)
#       log/
#       Makefile

SHELL := /bin/bash

# ---- Paths ---------------------------------------------------------------------------------------
TB_DIR       := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
COYOTE_DIR   ?= $(TB_DIR)/../Coyote-mrc
NETSTACK_HLS ?= $(COYOTE_DIR)/hw/services/network/hls
XILINX_HLS   ?= /tools/Xilinx/Vitis_HLS/2022.1

BUILD_DIR := $(TB_DIR)/build
LOG_DIR   := $(TB_DIR)/log
TARGET    := $(BUILD_DIR)/tb_ib_transport_protocol
LOG       := $(LOG_DIR)/tb_ib_transport_protocol.log

# ---- Sources -------------------------------------------------------------------------------------
TB_SRCS  := $(filter-out $(TB_DIR)/src/dut_top.cpp,$(wildcard $(TB_DIR)/src/*.cpp))
TB_HDRS  := $(wildcard $(TB_DIR)/include/tb/*.hpp $(TB_DIR)/include/tb/*.h) \
            $(TB_DIR)/include/config/rocev2_config.hpp $(TB_DIR)/include/config/tests_config.hpp

DUT_SRCS := $(TB_DIR)/src/dut_top.cpp \
            $(NETSTACK_HLS)/ib_transport_protocol/ib_transport_protocol.cpp \
            $(NETSTACK_HLS)/ib_transport_protocol/ib_utils.cpp \
            $(NETSTACK_HLS)/axi_utils.cpp

XILINX_HDRS := $(XILINX_HLS)/include

# ---- Flags ---------------------------------------------------------------------------------------
#   include/config -> rocev2_config.hpp (also needed by the module: #include <rocev2_config.hpp>)
#   include/tb     -> testbench headers
#   NETSTACK_HLS   -> "axi_utils.hpp", "ib_transport_protocol/ib_transport_protocol.hpp"
#   XILINX_HLS     -> ap_int.h, hls_stream.h
CXX      ?= g++
CXXFLAGS ?= -std=c++14 -O1 -w
INCLUDES := -I$(TB_DIR)/include/config -I$(TB_DIR)/include/tb -I$(NETSTACK_HLS) -I$(XILINX_HLS)/include

# ---- Targets -------------------------------------------------------------------------------------
$(TARGET): $(TB_SRCS) $(TB_HDRS) $(DUT_SRCS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $(TB_SRCS) $(DUT_SRCS) -o $@

# pipefail: 'make run' fails if the testbench returns non-zero (= some check failed)
run: $(TARGET) | $(LOG_DIR)
	set -o pipefail; $(TARGET) | tee $(LOG)

$(BUILD_DIR) $(LOG_DIR):
	mkdir -p $@

clean:
	rm -rf $(BUILD_DIR) $(LOG)

.PHONY: run clean