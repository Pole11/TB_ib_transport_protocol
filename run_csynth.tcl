# Vitis HLS synthesis of ib_transport_protocol_top at the clocks needed for 100G / 200G.
#
#   vitis_hls -f run_csynth.tcl                 (all targets)
#   TARGETS="100G" vitis_hls -f run_csynth.tcl  (subset)
#
# The data path is DATA_WIDTH bits wide (see rocev2_config.hpp), one beat per clock when II=1:
#   line rate [Gbps] = DATA_WIDTH * f_clk
#   100G @ 512 bit -> >= 195.3 MHz  (design target: 250 MHz = Coyote nclk, 128 Gbps)
#   200G @ 512 bit -> >= 390.6 MHz  (design target: 400 MHz, 204.8 Gbps)
# Passing needs: estimated period <= target - uncertainty, and II = 1 on the top-level stream
# interfaces (checked below via the achieved interval in the csynth report).

set TB_DIR       [file dirname [file normalize [info script]]]
set COYOTE_DIR   [file normalize $TB_DIR/../Coyote-mrc]
set NETSTACK_HLS $COYOTE_DIR/hw/services/network/hls
set CFLAGS       "-I$TB_DIR/include/config -I$TB_DIR/include/tb -I$NETSTACK_HLS -D__gmp_const=const"

set DATA_WIDTH 512

# name  clock period [ns]  uncertainty [ns]
set ALL_TARGETS {
    100G  4.0   1.08
    200G  2.5   0.68
}
set uncertainty_default 27%   ;# Vitis HLS default, only used if a target gives none

set selected [expr {[info exists ::env(TARGETS)] ? $::env(TARGETS) : "100G 200G"}]

open_project -reset ib_transport_syn_prj
set_top ib_transport_protocol_top

add_files $NETSTACK_HLS/ib_transport_protocol/ib_transport_protocol.cpp -cflags $CFLAGS
add_files $NETSTACK_HLS/ib_transport_protocol/ib_utils.cpp              -cflags $CFLAGS
add_files $NETSTACK_HLS/axi_utils.cpp                                   -cflags $CFLAGS
add_files $TB_DIR/src/dut_top.cpp                                       -cflags $CFLAGS

set results {}
foreach {name period unc} $ALL_TARGETS {
    if {[lsearch -exact $selected $name] < 0} { continue }

    open_solution -reset sol_$name
    set_part xcu55c-fsvh2892-2L-e
    create_clock -period $period -name default
    set_clock_uncertainty $unc default
    csynth_design

    # Parse the XML summary
    set xml $TB_DIR/ib_transport_syn_prj/sol_$name/syn/report/csynth.xml
    set f [open $xml]; set txt [read $f]; close $f
    regexp {<TargetClockPeriod>([0-9.]+)</TargetClockPeriod>}       $txt -> tgt
    regexp {<EstimatedClockPeriod>([0-9.]+)</EstimatedClockPeriod>} $txt -> est
    regexp {<ClockUncertainty>([0-9.]+)</ClockUncertainty>}         $txt -> unc_v
    set slack [expr {$tgt - $unc_v - $est}]
    set mhz   [expr {1000.0 / $est}]
    set gbps  [expr {$DATA_WIDTH * $mhz / 1000.0}]
    set ok    [expr {$slack >= 0 ? "PASS" : "FAIL"}]
    lappend results [format "%-5s target %.2f ns  est %.3f ns  unc %.2f  slack %+.3f ns  fmax %.0f MHz  -> %.1f Gbps  %s" \
                         $name $tgt $est $unc_v $slack $mhz $gbps $ok]
}

puts "\n================ TIMING SUMMARY ================"
foreach r $results { puts $r }
puts "Check II/latency of each stage in:"
puts "  ib_transport_syn_prj/sol_*/syn/report/ib_transport_protocol_top_csynth.rpt"
puts "================================================"
exit
