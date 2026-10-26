# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# Source in OpenROAD after DEF import, before any placement/timing repair.
# Explicit opt-in: sourcing this file does not modify the loaded design.
namespace eval livehd::preroute {}

# Reproduce the placement timing view only when explicitly requested. Supply
# preroute.json's timing_cut_pins before CTS/repair, and save the resulting SDC.
# Resolve exact identities rather than interpreting brackets in bus names as
# wildcard patterns. Validate the whole list before installing any exception.
proc livehd::preroute::apply_timing_cuts {labels} {
  set objects [dict create]
  set cell_pins [dict create]
  foreach port [get_ports *] {
    dict set objects [get_full_name $port] $port
  }
  foreach pin [get_pins -hierarchical *] {
    set name [get_full_name $pin]
    set separator [string last / $name]
    if {$separator < 0} {error "Timing pin has no instance separator: $name"}
    set label [string replace $name $separator $separator :]
    if {[dict exists $objects $label]} {error "Ambiguous timing-cut identity: $label"}
    dict set objects $label $pin
    dict set cell_pins $label 1
  }
  set clock_sources [dict create]
  foreach clock [all_clocks] {
    foreach pin [get_property $clock sources] {
      dict set clock_sources [get_full_name $pin] 1
    }
  }
  set resolved {}
  foreach label [lsort -unique $labels] {
    if {$label eq "" || ![dict exists $objects $label]} {
      error "Missing timing-cut endpoint: $label"
    }
    set object [dict get $objects $label]
    if {[dict exists $clock_sources [get_full_name $object]]
        || ([dict exists $cell_pins $label] && [llength [get_property $object clocks]])} {
      error "Timing-cut endpoint belongs to a clock network: $label"
    }
    lappend resolved $object
  }
  if {[llength $resolved]} {set_false_path -through $resolved}
  return [llength $resolved]
}

proc livehd::preroute::_fixed_routes {} {
  set block [ord::get_db_block]
  set nets {}
  set instances [dict create]
  set decoder [odb::dbWireDecoder]
  try {
    foreach net [$block getNets] {
      set wire [$net getWire]
      if {$wire eq "NULL"} {continue}
      set types [dict create]
      $decoder begin $wire
      while {[set opcode [$decoder next]] != $::odb::dbWireDecoder_END_DECODE} {
        if {$opcode == $::odb::dbWireDecoder_PATH || $opcode == $::odb::dbWireDecoder_SHORT
            || $opcode == $::odb::dbWireDecoder_JUNCTION} {
          dict set types [$decoder getWireType] 1
        }
      }
      # dbNet::getWireType may say ROUTED even when the encoded paths are
      # FIXED. Inspect the paths, not the net's default wire-type attribute.
      if {![dict exists $types FIXED]} {continue}
      if {[dict size $types] != 1} {
        error "pre-route net [$net getName] mixes FIXED and non-FIXED paths"
      }
      foreach iterm [$net getITerms] {
        set inst [$iterm getInst]
        if {[$inst getPlacementStatus] ne "FIRM"} {
          error "pre-route net [$net getName] touches non-FIRM instance [$inst getName]"
        }
        dict set instances [$inst getId] $inst
      }
      lappend nets $net
    }
  } finally {
    rename $decoder {}
  }
  # Explicit placement reservations survive ODB checkpoints. Include their
  # originally fixed bodies in the same movement/resize/clock-edit protection
  # as route endpoints. Movable occupants never become reservation locks.
  # Reject legacy lock policies and damaged reservations rather than rebuilding
  # them from cells which may already have moved.
  set property [odb::dbStringProperty_find $block livehd_preroute_space_v1]
  if {$property ne "NULL"} {
    set record [$property getValue]
    if {![dict exists $record policy] || [dict get $record policy] ne "endpoint_only"} {
      error "legacy corridor locks do not satisfy endpoint-only policy; reload the unreserved checkpoint"
    }
    dict for {name state} [dict get $record cells] {
      set inst [$block findInst $name]
      if {$inst eq "NULL" || [_instance_state $inst] ne $state} {
        error "fixed-route reserved cell changed: $name"
      }
      dict set instances [$inst getId] $inst
    }
    set live_blockages {}
    foreach blockage [$block getBlockages] {dict set live_blockages [$blockage getId] $blockage}
    dict for {id rect} [dict get $record blockages] {
      if {![dict exists $live_blockages $id]} {error "missing fixed-route placement reservation $id"}
      set blockage [dict get $live_blockages $id]
      set box [$blockage getBBox]
      if {[list [$box xMin] [$box yMin] [$box xMax] [$box yMax]] ne $rect
          || [$blockage isSoft] || [$blockage getMaxDensity] != 0} {
        error "fixed-route placement reservation changed: $id"
      }
    }
  }
  return [dict create nets $nets instances $instances]
}

proc livehd::preroute::protect_fixed_routes {} {
  set scope [_fixed_routes]
  set nets [dict get $scope nets]
  set instances [dict get $scope instances]
  # Validate the entire design before mutating any protection. A failure must
  # not leave an apparently protected subset of an invalid handoff.
  foreach net $nets {$net setDoNotTouch true}
  dict for {id inst} $instances {
    # FIRM prevents movement; dont_touch additionally prevents cell resizing
    # via another, unlocked net attached to the same instance.
    # Only protection owned by this handoff permits buffering ordinary inputs.
    # Preserve pre-existing user dont_touch semantics. The OpenROAD adapter
    # restores the flag before legalization and never relaxes fixed-net locks.
    if {![$inst isDoNotTouch]} {
      set permission [odb::dbBoolProperty_find $inst livehd_preroute_buffer_inputs_v1]
      if {$permission eq "NULL"} {
        odb::dbBoolProperty_create $inst livehd_preroute_buffer_inputs_v1 true
      } else {
        $permission setValue true
      }
    }
    $inst setDoNotTouch true
  }
  return [dict create nets [llength $nets] instances [dict size $instances]]
}

# OpenDB's instance dont_touch also prevents CTS from reconnecting a flop's
# clock terminal. FIRM continues to forbid movement while these clock-only
# operations run. Fixed data nets remain dont_touch throughout. Restore the
# instance locks even on error; on success verify exact wire encoding, data
# connectivity, master, origin, orientation and placement status.
proc livehd::preroute::_wire_state {net} {
  set wire [$net getWire]
  if {$wire eq "NULL"} {error "lost fixed wire on [$net getName]"}
  set encoded {}
  for {set i 0} {$i < [$wire length]} {incr i} {
    lappend encoded [$wire getOpcode $i] [$wire getData $i]
  }
  # Layer, RULE and VIA opcodes contain object ids, not their geometry.
  # Snapshot the definitions so in-place width/via edits cannot evade the check.
  set rules [dict create]
  set vias [dict create]
  set layers [dict create]
  set decoder [odb::dbWireDecoder]
  try {
    $decoder begin $wire
    while {[set opcode [$decoder next]] != $::odb::dbWireDecoder_END_DECODE} {
      if {$opcode == $::odb::dbWireDecoder_PATH || $opcode == $::odb::dbWireDecoder_SHORT
          || $opcode == $::odb::dbWireDecoder_JUNCTION} {
        set layer [$decoder getLayer]
        if {$layer eq "NULL"} {error "missing route layer on [$net getName]"}
        dict set layers [$layer getId] [list [$layer getName] [$layer getWidth]]
      }
      if {$opcode == $::odb::dbWireDecoder_RULE} {
        set rule [$decoder getRule]
        if {$rule eq "NULL"} {error "missing width rule on [$net getName]"}
        dict set rules [$rule getId] [list [[$rule getLayer] getName] [$rule getWidth] \
          [$rule getSpacing] [$rule getWireExtension] [$rule getResistance] \
          [$rule getCapacitance] [$rule getEdgeCapacitance]]
      } elseif {$opcode == $::odb::dbWireDecoder_VIA || $opcode == $::odb::dbWireDecoder_TECH_VIA} {
        if {$opcode == $::odb::dbWireDecoder_VIA} {
          set via [$decoder getVia]
        } else {
          set via [$decoder getTechVia]
        }
        if {$via eq "NULL"} {error "missing via definition on [$net getName]"}
        set key [list $opcode [$via getId]]
        if {![dict exists $vias $key]} {
          set boxes {}
          foreach box [$via getBoxes] {
            set layer [$box getTechLayer]
            lappend boxes [list [$layer getName] [$box xMin] [$box yMin] [$box xMax] [$box yMax]]
            # A via can change the active routing layer without a new PATH.
            if {[$layer getType] eq "ROUTING"} {
              dict set layers [$layer getId] [list [$layer getName] [$layer getWidth]]
            }
          }
          dict set vias $key [lsort $boxes]
        }
      }
    }
  } finally {
    rename $decoder {}
  }
  set pins {}
  foreach pin [$net getITerms] {lappend pins [list I [$pin getId]]}
  foreach pin [$net getBTerms] {lappend pins [list B [$pin getId]]}
  return [list $encoded [lsort $pins] [$net isDoNotTouch] $rules $vias $layers]
}

proc livehd::preroute::_instance_state {inst} {
  return [list [[$inst getMaster] getName] [$inst getLocation] [$inst getOrient] [$inst getPlacementStatus]]
}

proc livehd::preroute::_clock_edit {command} {
  protect_fixed_routes
  set scope [_fixed_routes]
  set block [ord::get_db_block]
  set cells [dict create]
  set wires [dict create]
  set release [dict create]
  dict for {id inst} [dict get $scope instances] {
    dict set cells [$inst getName] [_instance_state $inst]
    # Only locks installed by this handoff may be relaxed for CTS. A
    # pre-existing user lock keeps its ordinary OpenROAD semantics.
    set permission [odb::dbBoolProperty_find $inst livehd_preroute_buffer_inputs_v1]
    if {$permission ne "NULL" && [$permission getValue]} {
      dict set release [$inst getName] 1
    }
  }
  foreach net [dict get $scope nets] {
    dict set wires [$net getName] [_wire_state $net]
  }
  try {
    dict for {name ignored} $release {[$block findInst $name] setDoNotTouch false}
    set result [uplevel 1 $command]
  } finally {
    dict for {name ignored} $release {
      set inst [$block findInst $name]
      if {$inst ne "NULL"} {$inst setDoNotTouch true}
    }
  }
  dict for {name state} $cells {
    set inst [$block findInst $name]
    if {$inst eq "NULL" || [_instance_state $inst] ne $state} {
      error "CTS changed pre-route endpoint $name"
    }
  }
  dict for {name state} $wires {
    set net [$block findNet $name]
    if {$net eq "NULL" || [_wire_state $net] ne $state} {
      error "CTS changed pre-route wire or connectivity $name"
    }
  }
  return $result
}

# Use these two wrappers in place of the corresponding OpenROAD commands.
# Do not extend the temporary release to placement or timing repair.
proc livehd::preroute::repair_clock_inverters {} {
  return [_clock_edit [list ::repair_clock_inverters]]
}
proc livehd::preroute::clock_tree_synthesis {args} {
  return [_clock_edit [list ::clock_tree_synthesis {*}$args]]
}

# OpenROAD electrical/timing repair may buffer an unlocked net whose load is a
# protected instance, then abort when OpenDB refuses to reconnect that load.
# Exclude unsupported reconnections for this repair only. The scoped adapter
# permits ordinary inputs on handoff-owned locks; instance protection stays set.
proc livehd::preroute::_repair_with_fixed_endpoints {command} {
  protect_fixed_routes
  set scope [_fixed_routes]
  set incident [dict create]
  dict for {id inst} [dict get $scope instances] {
    set permission [odb::dbBoolProperty_find $inst livehd_preroute_buffer_inputs_v1]
    set buffer_inputs [expr {$permission ne "NULL" && [$permission getValue]}]
    foreach pin [$inst getITerms] {
      set net [$pin getNet]
      # Buffering loads on an internal net does not reconnect its driver.
      # Keep port nets protected: preserving their external name can require
      # moving the driver's output to a new net. No output pin is unlocked.
      if {$buffer_inputs && $net ne "NULL" && [$net getSigType] eq "SIGNAL" && ![$net isSpecial]
          && ([$pin getIoType] eq "INPUT"
              || ([$pin getIoType] eq "OUTPUT" && ![llength [$net getBTerms]]))} {
        continue
      }
      # Supply connectivity must remain editable for unrelated inserted or
      # removed cells. Endpoint instance protection still covers its PG pins.
      if {$net ne "NULL" && [$net getSigType] ni {POWER GROUND} && ![$net isDoNotTouch]} {
        dict set incident [$net getId] $net
      }
    }
  }
  try {
    dict for {id net} $incident {$net setDoNotTouch true}
    return [uplevel 1 $command]
  } finally {
    dict for {id net} $incident {$net setDoNotTouch false}
  }
}

proc livehd::preroute::repair_design {args} {
  return [_repair_with_fixed_endpoints [list ::repair_design {*}$args]]
}

proc livehd::preroute::repair_timing {args} {
  return [_repair_with_fixed_endpoints [list ::repair_timing {*}$args]]
}

# Use this predicate to skip TAPCELL_TCL when LiveHD supplied real taps. It does
# not certify pattern completeness or placement legality; run check_placement
# independently. No prefix/sidecar is needed: the platform master is authoritative.
proc livehd::preroute::has_fixed_taps {master} {
  if {$master eq ""} {error "tap master must not be empty"}
  set found 0
  foreach inst [[ord::get_db_block] getInsts] {
    if {[[$inst getMaster] getName] ne $master} {continue}
    if {[$inst getPlacementStatus] ni {FIRM LOCKED}} {
      error "supplied tap [$inst getName] is not fixed"
    }
    set found 1
  }
  return $found
}
