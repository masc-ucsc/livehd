# legroute.final F5: OpenROAD's DRC checker on a routed DEF as written by LiveHD, with no rerouting (the same rule
# engine as an OpenROAD-routed baseline). Global routing only builds the GCell grid the checker needs; the wires
# in the DEF are what is checked. Prints "LEGROUTE_DRC <markers>" and leaves the report in OUT.
#   LEFS="tech.lef cells.lef" DEF=out.def OUT=drc.rpt [FASTROUTE=platform/fastroute.tcl] \
#     openroad -no_init -exit legroute_drc.tcl
foreach lef $::env(LEFS) {
  read_lef $lef
}
read_def $::env(DEF)
if {[info exists ::env(FASTROUTE)]} {
  source $::env(FASTROUTE)
}
global_route -allow_congestion
drt::check_drc -output_file $::env(OUT)
set markers 0
set report [open $::env(OUT)]
while {[gets $report line] >= 0} {
  if {[string match "violation type:*" [string trim $line]]} {
    incr markers
  }
}
close $report
puts "LEGROUTE_DRC $markers"
