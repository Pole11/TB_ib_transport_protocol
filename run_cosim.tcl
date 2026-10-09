# Vitis HLS C synthesis + C/RTL co-simulation with waveform dump.
#
#   vitis_hls -f run_cosim.tcl
#
# Waveform: ib_transport_tb_prj/solution1/sim/verilog/*.wdb
#   open with: xsim --gui <file>.wdb

set TB_DIR       [file dirname [file normalize [info script]]]
set COYOTE_DIR   [file normalize $TB_DIR/../Coyote-mrc]
set NETSTACK_HLS $COYOTE_DIR/hw/services/network/hls
set CFLAGS       "-I$TB_DIR/include/config -I$TB_DIR/include/tb -I$NETSTACK_HLS -D__gmp_const=const"

open_project -reset ib_transport_tb_prj
set_top ib_transport_protocol_top

add_files     $NETSTACK_HLS/ib_transport_protocol/ib_transport_protocol.cpp -cflags $CFLAGS
add_files     $NETSTACK_HLS/ib_transport_protocol/ib_utils.cpp              -cflags $CFLAGS
add_files     $NETSTACK_HLS/axi_utils.cpp                                   -cflags $CFLAGS
add_files     $TB_DIR/src/dut_ib_transport_protocol_top.cpp                                       -cflags $CFLAGS
add_files -tb $TB_DIR/src/main.cpp                                          -cflags $CFLAGS

open_solution -reset solution1
set_part xcu55c-fsvh2892-2L-e
create_clock -period 4 -name default

csim_design
csynth_design
# -trace_level port is lighter; use all for internal signals. Drop -wave_debug on headless machines.
cosim_design -trace_level all -wave_debug -tool xsim
exit
