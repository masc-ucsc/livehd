# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Independent OpenDB geometry validation; no LiveHD parser or route oracle.
# Source after preroute_handoff.tcl. These procedures do nothing until called.
namespace eval livehd::preroute {}

proc livehd::preroute::_rect {shape} {
  return [list [[$shape getTechLayer] getName] [$shape xMin] [$shape yMin] [$shape xMax] [$shape yMax]]
}
proc livehd::preroute::_touch {a b {positive 0}} {
  if {[lindex $a 0] ne [lindex $b 0]} {return 0}
  set dx [expr {min([lindex $a 3],[lindex $b 3])-max([lindex $a 1],[lindex $b 1])}]
  set dy [expr {min([lindex $a 4],[lindex $b 4])-max([lindex $a 2],[lindex $b 2])}]
  if {$positive} {return [expr {$dx > 0 && $dy > 0}]}
  return [expr {$dx >= 0 && $dy >= 0 && ($dx > 0 || $dy > 0)}]
}
proc livehd::preroute::_root {parents_var index} {
  upvar 1 $parents_var parents
  while {[lindex $parents $index] != $index} {
    set next [lindex $parents $index]
    lset parents $index [lindex $parents $next]
    set index $next
  }
  return $index
}
proc livehd::preroute::_join {parents_var a b} {
  upvar 1 $parents_var parents
  set a [_root parents $a]
  set b [_root parents $b]
  if {$a != $b} {lset parents $b $a}
}
proc livehd::preroute::_append_shape {shape shapes_var joins_var} {
  upvar 1 $shapes_var shapes $joins_var joins
  if {![$shape isVia]} {
    if {[$shape getTechLayer] eq "NULL"} {error "wire shape has no layer"}
    lappend shapes [_rect $shape]
    return
  }
  lassign [$shape getViaXY] x y
  set via [$shape getTechVia]
  if {$via eq "NULL"} {set via [$shape getVia]}
  if {$via eq "NULL"} {error "via shape has no via definition"}
  set metals {}; set cuts {}; set layers {}
  foreach box [$via getBoxes] {
    set layer [$box getTechLayer]
    set rect [list [$layer getName] [expr {[$box xMin]+$x}] [expr {[$box yMin]+$y}] \
                                  [expr {[$box xMax]+$x}] [expr {[$box yMax]+$y}]]
    if {[$layer getType] eq "CUT"} {
      lappend cuts $rect
    } elseif {[$layer getType] eq "ROUTING"} {
      lappend metals [llength $shapes]
      dict set layers [$layer getName] 1
      lappend shapes $rect
    } else {error "unsupported via layer type"}
  }
  if {[dict size $layers] != 2 || ![llength $cuts]} {error "via must join exactly two metal layers through cuts"}
  foreach cut $cuts {
    set touched {}; set touched_layers {}
    foreach index $metals {
      set metal [lindex $shapes $index]
      # Geometry overlap is evaluated on the landing layer; the cut supplies
      # the explicit cross-layer connection, never coincident metal alone.
      set projected [lreplace $cut 0 0 [lindex $metal 0]]
      if {[_touch $metal $projected 1]} {
        lappend touched $index
        dict set touched_layers [lindex $metal 0] 1
      }
    }
    if {[dict size $touched_layers] != 2} {error "via cut misses a metal landing"}
    foreach index [lrange $touched 1 end] {lappend joins [list [lindex $touched 0] $index]}
  }
}
# dbWirePathItr omits RECT patches. Decode those separately; getRect needs
# the OpenDB SWIG OUTPUT typemap for its four integer-reference arguments.
# An older binding must fail instead of silently validating incomplete metal.
proc livehd::preroute::_append_wire_patches {wire shapes_var} {
  upvar 1 $shapes_var shapes
  set decoder [odb::dbWireDecoder]
  set point {}
  try {
    $decoder begin $wire
    while {[set opcode [$decoder next]] != $::odb::dbWireDecoder_END_DECODE} {
      if {$opcode == $::odb::dbWireDecoder_POINT} {
        set point [$decoder getPoint]
      } elseif {$opcode == $::odb::dbWireDecoder_POINT_EXT} {
        set point [lrange [$decoder getPoint_ext] 0 1]
      } elseif {$opcode == $::odb::dbWireDecoder_RECT} {
        if {[llength $point] != 2} {error "DEF patch has no anchor"}
        if {[catch {$decoder getRect} offsets] || [llength $offsets] != 4} {
          error "OpenDB getRect output binding is required to validate DEF patches"
        }
        lassign $point x y
        lassign $offsets xl yl xh yh
        if {$xl >= $xh || $yl >= $yh} {error "invalid DEF patch rectangle"}
        lappend shapes [list [[$decoder getLayer] getName] [expr {$x+$xl}] [expr {$y+$yl}] \
                                                         [expr {$x+$xh}] [expr {$y+$yh}]]
      } elseif {$opcode == $::odb::dbWireDecoder_PATH || $opcode == $::odb::dbWireDecoder_SHORT
                || $opcode == $::odb::dbWireDecoder_JUNCTION} {
        set point {}
      }
    }
  } finally {rename $decoder {}}
}
proc livehd::preroute::check_fixed_connectivity {} {
  set scope [_fixed_routes]
  set checked 0; set total_shapes 0; set total_pins 0
  foreach net [dict get $scope nets] {
    if {[llength [$net getBTerms]]} {error "phase-1 fixed net [$net getName] has IO terminals"}
    set shapes {}; set joins {}; set pin_groups {}
    set iter [odb::dbWirePathItr]
    set path [odb::dbWirePath]
    set path_shape [odb::dbWirePathShape]
    try {
      $iter begin [$net getWire]
      while {[$iter getNextPath $path]} {
        while {[$iter getNextShape $path_shape]} {
          _append_shape [$path_shape cget -shape] shapes joins
        }
      }
    } finally {
      rename $path_shape {}; rename $path {}; rename $iter {}
    }
    _append_wire_patches [$net getWire] shapes
    set wire_count [llength $shapes]
    if {!$wire_count} {error "fixed net [$net getName] has no metal"}
    foreach pin [$net getITerms] {
      set first [llength $shapes]
      foreach port [[$pin getMTerm] getMPins] {
        if {[llength [$port getPolygonGeometry]]} {error "polygon pin unsupported in fixed-net connectivity check"}
        foreach box [$port getGeometry] {
          if {[$box isVia]} {error "via pin unsupported in fixed-net connectivity check"}
        }
      }
      foreach geometry [$pin getGeometries] {
        lassign $geometry layer rect
        try {
          if {$layer eq "NULL" || [$layer getType] ne "ROUTING"} {error "unsupported physical pin geometry"}
          lappend shapes [list [$layer getName] [$rect xMin] [$rect yMin] [$rect xMax] [$rect yMax]]
        } finally {odb::delete_Rect $rect; if {[llength [info commands $rect]]} {rename $rect {}}}
      }
      if {$first == [llength $shapes]} {error "fixed net [$net getName] has an empty physical pin"}
      # Shapes/ports of a single logical pin are internally connected.
      for {set i [expr {$first+1}]} {$i < [llength $shapes]} {incr i} {lappend joins [list $first $i]}
      lappend pin_groups $first
    }
    if {[llength $pin_groups] < 2} {error "fixed net [$net getName] has fewer than two cell pins"}
    set parents {}; set by_layer {}; set i 0
    foreach shape $shapes {
      lappend parents $i
      dict lappend by_layer [lindex $shape 0] [list [lindex $shape 1] $i]
      incr i
    }
    foreach pair $joins {_join parents {*}$pair}
    # Sweep each layer by left edge. Point-only contacts are not conductive.
    dict for {layer entries} $by_layer {
      set active {}
      foreach entry [lsort -integer -index 0 $entries] {
        lassign $entry x i
        set current [lindex $shapes $i]
        set retained {}
        foreach j $active {
          set other [lindex $shapes $j]
          if {[lindex $other 3] < $x} {continue}
          lappend retained $j
          if {[_touch $current $other]} {_join parents $i $j}
        }
        lappend retained $i
        set active $retained
      }
    }
    set root [_root parents 0]
    for {set i 1} {$i < [llength $parents]} {incr i} {
      if {[_root parents $i] != $root} {error "open or disconnected metal on fixed net [$net getName]"}
    }
    incr checked
    incr total_shapes $wire_count
    incr total_pins [llength $pin_groups]
  }
  return [dict create nets $checked wire_shapes $total_shapes pins $total_pins]
}

# The DRC engine needs an ancillary GCELLGRID even before global routing.
# When absent, create one for this validation database; no metal or instances
# are changed. Prefer a separate checker database for pre-CTS validation.
proc livehd::preroute::check_fixed_routes {drc_file} {
  set result [check_fixed_connectivity]
  set block [ord::get_db_block]
  set grid [$block getGCellGrid]
  if {$grid eq "NULL"} {
    set die [$block getDieArea]
    try {
      set grid [odb::dbGCellGrid_create $block]
      # TritonRoute checks regions spanning seven gcells per axis. A fixed
      # bin count makes each worker retain far too much geometry on large dies.
      # Bound the auxiliary pitch to 10um while retaining at least 64 bins.
      set max_pitch [expr {max(1,10*[$block getDbUnitsPerMicron])}]
      set width [expr {[$die xMax]-[$die xMin]}]
      set height [expr {[$die yMax]-[$die yMin]}]
      set nx [expr {max(64,int(ceil(double($width)/$max_pitch)))}]
      set ny [expr {max(64,int(ceil(double($height)/$max_pitch)))}]
      set dx [expr {max(1,int(ceil(double($width)/$nx)))}]
      set dy [expr {max(1,int(ceil(double($height)/$ny)))}]
      $grid addGridPatternX [$die xMin] [expr {$nx+1}] $dx
      $grid addGridPatternY [$die yMin] [expr {$ny+1}] $dy
    } finally {rename $die {}}
  }
  if {![llength [$grid getGridX]] || ![llength [$grid getGridY]]} {error "empty validation GCELLGRID"}
  # Remove stale output: an absent/new nonempty report is always a failure.
  if {[file exists $drc_file]} {file delete $drc_file}
  drt::check_drc -output_file $drc_file
  if {![file exists $drc_file]} {error "independent DRC did not create its report"}
  if {[file size $drc_file] != 0} {error "independent DRC violations: $drc_file"}
  dict set result drc_report $drc_file
  return $result
}
