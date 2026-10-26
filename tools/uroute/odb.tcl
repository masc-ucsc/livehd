# SPDX-License-Identifier: BSD-3-Clause
# External OpenROAD adapter. Path variables are set by the typed Python CLI.
if {[catch {
 read_db $::env(LHD_UROUTE_INPUT_ODB)
 write_def $::env(LHD_UROUTE_OUTPUT_DEF)
 if {$::env(LHD_UROUTE_OUTPUT_WIRELENGTH) ne ""} {
  report_wire_length -net {*} -detailed_route -file $::env(LHD_UROUTE_OUTPUT_WIRELENGTH)
 }
 if {$::env(LHD_UROUTE_OUTPUT_OVERFLOW) ne ""} {
  set block [ord::get_db_block]
  set core [$block getCoreArea]
  set die [$block getDieArea]
  set grid [$block getGCellGrid]
  if {$grid eq "NULL"} {error "ODB has no global-route congestion grid"}
  set xs [$grid getGridX];set ys [$grid getGridY]
  set nx [llength $xs];set ny [llength $ys]
  if {$nx<1 || $ny<1 || [$core dx]<=0 || [$core dy]<=0} {error "Invalid ODB congestion/core geometry"}
  if {[lindex $xs 0]>[$core xMin] || [lindex $ys 0]>[$core yMin] || [$die xMax]<[$core xMax] || [$die yMax]<[$core yMax]} {
   error "Congestion grid does not cover the core"
  }
  set bx 256;set by 256
  set dx [expr {double([$core dx])/$bx}];set dy [expr {double([$core dy])/$by}]
  array set values {}
  set capacity_total 0.0;set usage_total 0.0;set overflow_total 0.0;set layers 0;set positive 0
  foreach layer [[ord::get_db_tech] getLayers] {
   if {[$layer getType] ne "ROUTING"} {continue}
   incr layers
   puts "UROUTE_ODB_RESOURCE_LAYER [$layer getName] grid=${nx}x${ny}"
   flush stdout
   for {set x 0} {$x<$nx} {incr x} {
    set xl [lindex $xs $x]
    set xh [expr {$x+1<$nx ? [lindex $xs [expr {$x+1}]] : [$die xMax]}]
    if {$xh<=$xl} {error "Non-increasing global-route X grid"}
    for {set y 0} {$y<$ny} {incr y} {
     set capacity [$grid getCapacity $layer $x $y]
     set usage [$grid getUsage $layer $x $y]
     if {$capacity<0 || $usage<0} {error "Negative global-route resource data"}
     set capacity_total [expr {$capacity_total+$capacity}]
     set usage_total [expr {$usage_total+$usage}]
     set overflow [expr {max(0.0,$usage-$capacity)}]
     set overflow_total [expr {$overflow_total+$overflow}]
     if {$overflow==0} {continue}
     incr positive
     set yl [lindex $ys $y]
     set yh [expr {$y+1<$ny ? [lindex $ys [expr {$y+1}]] : [$die yMax]}]
     if {$yh<=$yl} {error "Non-increasing global-route Y grid"}
     set ax [expr {max($xl,[$core xMin])}];set az [expr {min($xh,[$core xMax])}]
     set ay [expr {max($yl,[$core yMin])}];set aw [expr {min($yh,[$core yMax])}]
     if {$az<=$ax || $aw<=$ay} {continue}
     set first_x [expr {max(0,int(floor(($ax-[$core xMin])/$dx)))}]
     set last_x [expr {min($bx-1,int(ceil(($az-[$core xMin])/$dx))-1)}]
     set first_y [expr {max(0,int(floor(($ay-[$core yMin])/$dy)))}]
     set last_y [expr {min($by-1,int(ceil(($aw-[$core yMin])/$dy))-1)}]
     for {set i $first_x} {$i<=$last_x} {incr i} {
      set ox [expr {max(0.0,min($az,[$core xMin]+($i+1)*$dx)-max($ax,[$core xMin]+$i*$dx))}]
      for {set j $first_y} {$j<=$last_y} {incr j} {
       set oy [expr {max(0.0,min($aw,[$core yMin]+($j+1)*$dy)-max($ay,[$core yMin]+$j*$dy))}]
       set key [expr {$i*$by+$j}]
       if {![info exists values($key)]} {set values($key) 0.0}
       set values($key) [expr {$values($key)+$overflow*$ox*$oy/($dx*$dy)}]
      }
     }
    }
   }
  }
  puts "UROUTE_ODB_RESOURCE_CENSUS capacity=$capacity_total usage=$usage_total overflow=$overflow_total"
  flush stdout
  # OpenDB lazily manufactures zero-filled maps when resource data is absent.
  # A positive capacity census distinguishes stored resources from that case.
  if {$capacity_total<=0} {error "ODB has no populated global-route resource map; refusing fabricated zero overflow"}
  set output [open $::env(LHD_UROUTE_OUTPUT_OVERFLOW) w]
  puts $output "UROUTE_GRT1"
  puts $output [join [list $bx $by [$block getDbUnitsPerMicron] [$core xMin] [$core yMin] [$core xMax] [$core yMax]] "\t"]
  puts $output [join [list $nx $ny $layers $capacity_total $usage_total $overflow_total $positive] "\t"]
  for {set i 0} {$i<$bx} {incr i} {
   for {set j 0} {$j<$by} {incr j} {
    set key [expr {$i*$by+$j}]
    set value [expr {[info exists values($key)] ? $values($key) : 0.0}]
    puts $output "$i\t$j\t$value"
   }
  }
  close $output
 }
 puts "UROUTE_ODB_EXPORT_OK"
} message options]} {
 puts stderr "UROUTE_ODB_EXPORT_FAILED: $message"
 puts stderr [dict get $options -errorinfo]
 exit 1
}
exit 0
