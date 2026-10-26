# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Explicit placement reservations around fixed wires on the two lowest routing
# layers (pin access and escape); sourcing only defines procedures.
source [file join [file dirname [info script]] preroute_handoff.tcl]
source [file join [file dirname [info script]] preroute_validate.tcl]
namespace eval livehd::preroute {}
proc livehd::preroute::_merge_intervals {values} {
  set out {}
  foreach v [lsort -integer -index 0 $values] {
    lassign $v lo hi
    if {$hi <= $lo} {continue}
    if {[llength $out] && $lo <= [lindex $out end 1]} {
      lset out end 1 [expr {max($hi,[lindex $out end 1])}]
    } else {lappend out [list $lo $hi]}
  }
  return $out
}
# Placement rows whose pitch-high Y bands intersect [yl, yh).
proc livehd::preroute::_rows_in_band {bins pitch yl yh} {
  set rows {}
  for {set k [expr {int(floor(double($yl)/$pitch))}]} {$k <= int(floor(double($yh-1)/$pitch))} {incr k} {
    if {[dict exists $bins $k]} {foreach id [dict get $bins $k] {dict set rows $id 1}}
  }
  return $rows
}
proc livehd::preroute::_plan_route_space {} {
  set block [ord::get_db_block]
  set scope [_fixed_routes]
  set tech [[ord::get_db] getTech]
  set low_layers {}
  foreach level {1 2} {
    set routing_layer [$tech findRoutingLayer $level]
    if {$routing_layer eq "NULL"} {error "fewer than two routing layers"}
    lappend low_layers [$routing_layer getName]
  }
  set rows {};set pitch 0
  foreach row [$block getRows] {
    if {[$row getDirection] ne "HORIZONTAL"} {error "unsupported vertical placement row"}
    set box [$row getBBox]
    try {set data [list [$box xMin] [$box yMin] [$box xMax] [$box yMax] [$row getSpacing]]} finally {rename $box {}}
    lassign $data xl yl xh yh step
    if {$step <= 0 || $yh <= $yl} {error "invalid placement row"}
    if {$pitch == 0 || $yh-$yl < $pitch} {set pitch [expr {$yh-$yl}]}
    dict set rows [$row getId] $data
  }
  if {$pitch == 0} {error "no placement rows"}
  set bins {}
  dict for {id row} $rows {
    lassign $row xl yl xh yh step
    for {set k [expr {int(floor(double($yl)/$pitch))}]} {$k <= int(floor(double($yh-1)/$pitch))} {incr k} {dict lappend bins $k $id}
  }
  set intervals {}
  foreach net [dict get $scope nets] {
    set shapes {};set joins {}
    _append_wire_shapes $net shapes joins
    foreach shape $shapes {
      lassign $shape layer xl yl xh yh
      if {$layer ni $low_layers} {continue}
      set tech_layer [$tech findLayer $layer]
      set margin [expr {4*[$tech_layer getWidth]}]
      set xl [expr {$xl-$margin}];set yl [expr {$yl-$margin}]
      set xh [expr {$xh+$margin}];set yh [expr {$yh+$margin}]
      dict for {id unused} [_rows_in_band $bins $pitch $yl $yh] {
        lassign [dict get $rows $id] rx ry rxh ryh step
        if {$yl >= $ryh || $yh <= $ry || $xl >= $rxh || $xh <= $rx} {continue}
        set lo [expr {max($rx,$rx+int(floor(double($xl-$rx)/$step))*$step)}]
        set hi [expr {min($rxh,$rx+int(ceil(double($xh-$rx)/$step))*$step)}]
        if {$lo < $hi} {dict lappend intervals $id [list $lo $hi]}
      }
    }
  }
  dict for {id values} $intervals {dict set intervals $id [_merge_intervals $values]}
  set cells {};set occupied {}
  foreach inst [$block getInsts] {
    # Only immutable bodies are holes in the reserved corridor. Movable
    # occupants must leave at the next detailed-placement step; do not turn
    # them into additional fixed-route endpoints.
    if {[$inst getPlacementStatus] ni {FIRM LOCKED}} {continue}
    set box [$inst getBBox]
    lassign [list [$box xMin] [$box yMin] [$box xMax] [$box yMax]] xl yl xh yh
    dict for {id unused} [_rows_in_band $bins $pitch $yl $yh] {
      if {![dict exists $intervals $id]} {continue}
      lassign [dict get $rows $id] rx ry rxh ryh step
      if {$yl >= $ryh || $yh <= $ry} {continue}
      foreach segment [dict get $intervals $id] {
        lassign $segment lo hi
        if {$xl < $hi && $xh > $lo} {
          if {[$inst getPlacementStatus] ni {PLACED FIRM LOCKED}} {error "unplaced cell in fixed-route corridor"}
          dict set cells [$inst getName] [_instance_state $inst]
          dict lappend occupied $id [list $xl $xh]
          break
        }
      }
    }
  }
  set blocks {}
  dict for {id values} $intervals {
    lassign [dict get $rows $id] rx ry rxh ryh step
    set used {}
    if {[dict exists $occupied $id]} {set used [_merge_intervals [dict get $occupied $id]]}
    foreach segment $values {
      lassign $segment cursor hi
      foreach span $used {
        lassign $span lo end
        if {$end <= $cursor} {continue}
        if {$lo >= $hi} {break}
        if {$lo > $cursor} {lappend blocks [list $cursor $ry $lo $ryh]}
        set cursor [expr {max($cursor,$end)}]
        if {$cursor >= $hi} {break}
      }
      if {$cursor < $hi} {lappend blocks [list $cursor $ry $hi $ryh]}
    }
  }
  return [dict create cells $cells blockages $blocks]
}

# Reserve low-layer fixed-wire corridors without adding instance locks.
# No instances move here. Apply during downstream CTS/repair before its next
# detailed-placement step, which must evacuate overlapping movable cells before
# pin access or routing. Existing fixed bodies are exempted from blockages.
proc livehd::preroute::reserve_fixed_route_space {} {
  set block [ord::get_db_block]
  set property [odb::dbStringProperty_find $block livehd_preroute_space_v1]
  # _fixed_routes rejects a legacy lock policy recorded in this property, so
  # any record read below already has the endpoint_only policy.
  set scope [_fixed_routes]
  if {$property ne "NULL"} {
    set record [$property getValue]
    set current {}
    foreach net [dict get $scope nets] {dict set current [$net getName] [_wire_state $net]}
    if {$current ne [dict get $record wires]} {error "fixed routes changed after placement reservation"}
    return [dict get $record summary]
  }
  if {![llength [dict get $scope nets]]} {return [dict create instances 0 blockages 0]}
  set plan [_plan_route_space]
  set created {};set reserved {};set blocks {};set net_touch {}
  # Validate all states before mutating placement or creating any blockages.
  dict for {name state} [dict get $plan cells] {
    set inst [$block findInst $name]
    if {$inst eq "NULL" || [_instance_state $inst] ne $state} {error "reservation input changed: $name"}
    dict set reserved $name $state
  }
  foreach net [dict get $scope nets] {dict set net_touch [$net getName] [$net isDoNotTouch]}
  try {
    foreach rect [dict get $plan blockages] {
      set blockage [odb::dbBlockage_create $block {*}$rect]
      if {$blockage eq "NULL"} {error "could not create fixed-route placement reservation"}
      lappend created $blockage
      dict set blocks [$blockage getId] $rect
    }
    # Fixed nets were already validated; protect before recording their states.
    foreach net [dict get $scope nets] {$net setDoNotTouch true}
    set wires {}
    foreach net [dict get $scope nets] {dict set wires [$net getName] [_wire_state $net]}
    set summary [dict create instances [dict size $reserved] blockages [llength $created]]
    set record [dict create policy endpoint_only cells $reserved blockages $blocks wires $wires summary $summary]
    set property [odb::dbStringProperty_create $block livehd_preroute_space_v1 $record]
    if {$property eq "NULL"} {error "could not record fixed-route placement reservations"}
  } on error {message opts} {
    foreach blockage $created {odb::dbBlockage_destroy $blockage}
    dict for {name touch} $net_touch {[$block findNet $name] setDoNotTouch $touch}
    return -options $opts $message
  }
  return $summary
}
