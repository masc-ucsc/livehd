# Run with the OpenSTA embedded in the pinned HighTide OpenROAD.
set here [file dirname [info script]]
if {[llength [info commands read_lef]]} {read_lef [file join $here clock.lef]}
read_liberty [file join $here clock.lib]
read_verilog [file join $here clock.v]
link_design clock_phase
create_clock -name master -period 1000 [get_ports clk]
create_generated_clock -name half -master_clock master -source [get_pins div1/CLK] -divide_by 2 [get_pins div1/Q]
create_generated_clock -name quarter -master_clock half -source [get_pins div2/CLK] -divide_by 2 [get_pins div2/Q]
set_propagated_clock [all_clocks]
sta::worst_slack -max
proc check_rise {pin expected} {
  set vertex [lindex [[sta::find_pin $pin] vertices] 0]
  set path [sta::vertex_worst_arrival_path_rf $vertex rise max]
  if {$path eq "NULL"} {error "no propagated rise at $pin"}
  set time [expr {1e12 * [$path arrival]}]
  puts "$pin $time ps"
  if {abs($time - $expected) > 0.01} {error "$pin expected $expected ps, observed $time ps"}
}
check_rise div1/CLK 20
check_rise div2/CLK 50
check_rise capture/CLK 80
check_rise capture_inv/CLK 510
