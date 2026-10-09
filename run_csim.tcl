# Vitis HLS C-simulation of the ib_transport_protocol testbench.
#
#   vitis_hls -f run_csim.tcl                      (run from any directory)
#
# Layout (same as the Makefile):
#   Coyote/hw/services/network/hls     <- DUT sources
#   TB_ib_transport_protocol/          <- this directory

set TB_DIR       [file dirname [file normalize [info script]]]
set COYOTE_DIR   [file normalize $TB_DIR/../Coyote-mrc]
set NETSTACK_HLS $COYOTE_DIR/hw/services/network/hls
set CFLAGS       "-I$TB_DIR/include/config -I$TB_DIR/include/tb -I$NETSTACK_HLS -D__gmp_const=const"

open_project -reset ib_transport_tb_prj
# The top is irrelevant for C simulation (only main() in the testbench is executed).
set_top ib_transport_protocol_top

add_files     $NETSTACK_HLS/ib_transport_protocol/ib_transport_protocol.cpp -cflags $CFLAGS
add_files     $NETSTACK_HLS/ib_transport_protocol/ib_utils.cpp              -cflags $CFLAGS
add_files     $NETSTACK_HLS/axi_utils.cpp                                   -cflags $CFLAGS
add_files     $TB_DIR/src/dut_ib_transport_protocol_top.cpp                                       -cflags $CFLAGS
add_files -tb $TB_DIR/src/main.cpp                                          -cflags $CFLAGS

open_solution -reset solution1
set_part xcu55c-fsvh2892-2L-e

csim_design
exit
