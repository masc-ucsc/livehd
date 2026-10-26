#!/usr/bin/env python3
"""Exact-DEF import and native timing, without synthesis or external timers."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile


def check_named_clocks(root, binary, lef):
    clock_lef = root / "clock.lef"
    clock_lef.write_text("""VERSION 5.8 ;
MACRO FF
 CLASS CORE ; ORIGIN 0 0 ; SIZE 1 BY 1 ; SITE core ;
 PIN CLK DIRECTION INPUT ; USE CLOCK ; PORT LAYER M1 ; RECT 0.1 0.1 0.2 0.2 ; END END CLK
 PIN D DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.1 0.3 0.2 0.4 ; END END D
 PIN Q DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.8 0.8 0.9 0.9 ; END END Q
END FF
END LIBRARY
""")
    clock_lib = root / "clock.lib"
    clock_lib.write_text("""library(test){time_unit:"1ps";capacitive_load_unit(1,ff);default_input_pin_cap:1;
cell(FF){ff(IQ,IQN){clocked_on:CLK;next_state:D;}
pin(CLK){direction:input;clock:true;}
pin(D){direction:input;timing(){related_pin:CLK;timing_type:setup_rising;
rise_constraint(scalar){values("5");}fall_constraint(scalar){values("8");}}}
pin(Q){direction:output;timing(){related_pin:CLK;timing_type:rising_edge;timing_sense:non_unate;
cell_rise(scalar){values("10");}cell_fall(scalar){values("20");}
rise_transition(scalar){values("2");}fall_transition(scalar){values("3");}}}}}
""")
    design = root / "clock.def"
    design.write_text("""VERSION 5.8 ;
DIVIDERCHAR "/" ; BUSBITCHARS "[]" ; DESIGN top ;
UNITS DISTANCE MICRONS 1000 ; DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 2 ;
- fa FF + PLACED ( 1000 1000 ) N ;
- fb FF + PLACED ( 3000 1000 ) N ;
END COMPONENTS
PINS 5 ;
- ca + NET ca + DIRECTION INPUT + USE SIGNAL + FIXED ( 0 0 ) N ;
- cb + NET cb + DIRECTION INPUT + USE SIGNAL + FIXED ( 0 1000 ) N ;
- d + NET d + DIRECTION INPUT + USE SIGNAL + FIXED ( 0 2000 ) N ;
- za + NET za + DIRECTION OUTPUT + USE SIGNAL + FIXED ( 10000 1000 ) N ;
- zb + NET zb + DIRECTION OUTPUT + USE SIGNAL + FIXED ( 10000 2000 ) N ;
END PINS
NETS 5 ;
- ca ( PIN ca ) ( fa CLK ) ;
- cb ( PIN cb ) ( fb CLK ) ;
- d ( PIN d ) ( fb D ) ;
- za ( fa Q ) ( PIN za ) ;
- zb ( fb Q ) ( fa D ) ( PIN zb ) ;
END NETS
END DESIGN
""")
    output = root / "named.json"
    config = root / "named.config.json"
    cfg = {"liberty": [str(clock_lib)], "lefs": [str(lef), str(clock_lef)],
           "def": str(design), "output": str(output), "constraints_complete": True,
           "clocks": {"a": {"period_ps": 100, "roots": ["ca"]},
                      "b": {"period_ps": 200, "roots": ["cb"]}},
           "clock_aliases": {"alias_a": {"source": "a", "roots": ["ca"]}},
           "asynchronous": [["alias_a", "b"]], "include_domains": True,
           "inputs": {"d": {"arrival_ps": [10, 10], "clock": "b"}},
           "outputs": {"za": {"required_ps": [100, 100], "clock": "a"},
                       "zb": {"required_ps": [200, 200], "clock": "b"}}}
    config.write_text(json.dumps(cfg))
    subprocess.run([binary, config], check=True)
    result = json.loads(output.read_text())
    assert result["certified"], result["unsupported"]
    assert result["wns_ps"] == 80
    assert result["achievable_period_ps"] is None
    assert result["characterized_driver_boundary"] is False
    assert result["bounded_driver_boundary"] is False
    assert result["table_boundary_driver_fits"] == 0
    assert result["clocks"]["a"]["achievable_period_ps"] == 20
    assert result["clocks"]["b"]["wns_ps"] == 180
    domain_b = next(d for d in result["domains"] if d["launch_clock"] == "b")
    assert domain_b["pins"]["fa:D"][1]["arrival_ps"] == 20
    assert domain_b["pins"]["fa:D"][1]["required_ps"] is None
    assert "period_objective" not in result
    one_clock = json.loads(json.dumps(cfg))
    one_clock.pop("asynchronous")
    one_clock["clocks"] = {"a": {"period_ps": 100, "roots": ["ca", "cb"]}}
    one_clock["inputs"]["d"]["clock"] = "a"
    one_clock["outputs"]["zb"] = {"required_ps": [100, 100], "clock": "a"}
    one_clock["output"] = str(root / "objective_control.json")
    config.write_text(json.dumps(one_clock))
    subprocess.run([binary, config], check=True)
    control = json.loads((root / "objective_control.json").read_text())
    one_clock.update(period_objective=True, output=str(root / "objective.json"))
    config.write_text(json.dumps(one_clock))
    subprocess.run([binary, config], check=True)
    observed = json.loads((root / "objective.json").read_text())
    objective = observed["period_objective"]
    assert objective["supported"] and not objective["optimizer_feedback_enabled"]
    assert objective["achievable_period_ps"] == control["achievable_period_ps"]
    assert abs(objective["endpoint_mass"] - 1) < 1e-12
    assert abs(objective["boundary_mass"] - 1) < 1e-12
    for field in ["wns_ps", "tns_ps", "pins", "domains", "clocks"]:
        assert observed[field] == control[field], field
    boundary_cfg = dict(one_clock, characterized_driver_boundary=True,
                        output=str(root / "boundary.json"))
    config.write_text(json.dumps(boundary_cfg))
    subprocess.run([binary, config], check=True)
    boundary = json.loads((root / "boundary.json").read_text())
    assert boundary["characterized_driver_boundary"] is True
    assert boundary["table_boundary_driver_fits"] == 0
    for field in ["wns_ps", "tns_ps", "pins", "domains", "clocks"]:
        assert boundary[field] == control[field], field
    bounded_cfg = dict(one_clock, bounded_driver_boundary=True, output=str(root / "bounded.json"))
    config.write_text(json.dumps(bounded_cfg))
    subprocess.run([binary, config], check=True)
    bounded = json.loads((root / "bounded.json").read_text())
    assert bounded["bounded_driver_boundary"] is True
    assert bounded["full_load_boundary_driver_fits"] == 0
    assert bounded["crossing_boundary_driver_fits"] == 0
    for field in ["wns_ps", "tns_ps", "pins", "domains", "clocks"]:
        assert bounded[field] == control[field], field
    bounded_cfg.update(bounded_driver_boundary="true", output=str(root / "bounded_invalid.json"))
    config.write_text(json.dumps(bounded_cfg))
    assert subprocess.run([binary, config], capture_output=True).returncode != 0
    boundary_cfg.update(characterized_driver_boundary="true", output=str(root / "boundary_invalid.json"))
    config.write_text(json.dumps(boundary_cfg))
    assert subprocess.run([binary, config], capture_output=True).returncode != 0
    one_clock.update(period_objective="true", output=str(root / "objective_invalid.json"))
    config.write_text(json.dumps(one_clock))
    assert subprocess.run([binary, config], capture_output=True).returncode != 0
    sdc = root / "clock.sdc"
    sdc.write_text("""current_design top
create_clock -name a -period 100 [get_ports {ca}]
create_clock -name b -period 200 [get_ports {cb}]
set_clock_groups -asynchronous -group [get_clocks {a}] -group [get_clocks {b}]
set_input_delay 10 -clock [get_clocks {b}] -add_delay [get_ports {d}]
set_output_delay 0 -clock [get_clocks {a}] -add_delay [get_ports {za}]
set_output_delay 0 -clock [get_clocks {b}] -add_delay [get_ports {zb}]
""")
    for key in ["clocks", "clock_aliases", "asynchronous", "inputs", "outputs", "constraints_complete"]:
        cfg.pop(key)
    cfg.update(sdc=str(sdc), sdc_units={"time_ps": 1, "capacitance_ff": 1}, output=str(root / "sdc.json"))
    config.write_text(json.dumps(cfg))
    subprocess.run([binary, config], check=True)
    parsed = json.loads((root / "sdc.json").read_text())
    assert parsed["certified"], parsed["unsupported"]
    assert parsed["wns_ps"] == result["wns_ps"]
    cfg["clock"] = {"period_ps": 100, "roots": ["ca"]}
    cfg["output"] = str(root / "conflict.json")
    config.write_text(json.dumps(cfg))
    assert subprocess.run([binary, config], capture_output=True).returncode != 0


def main():
    binary = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        lef = root / "cells.lef"
        lef.write_text("""VERSION 5.8 ;
BUSBITCHARS "[]" ; DIVIDERCHAR "/" ;
UNITS DATABASE MICRONS 1000 ; END UNITS
LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.1 ; WIDTH 0.05 ; END M1
SITE core CLASS CORE ; SIZE 1 BY 1 ; SYMMETRY X Y ; END core
MACRO INV
 CLASS CORE ; ORIGIN 0 0 ; SIZE 1 BY 1 ; SITE core ;
 PIN A DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.1 0.1 0.2 0.2 ; END END A
 PIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.8 0.8 0.9 0.9 ; END END Y
END INV
END LIBRARY
""")
        netlist = root / "signal.def"
        netlist.write_text("""VERSION 5.8 ;
DIVIDERCHAR "/" ; BUSBITCHARS "[]" ; DESIGN top ;
UNITS DISTANCE MICRONS 1000 ; DIEAREA ( 0 0 ) ( 10000 10000 ) ;
COMPONENTS 2 ;
- u INV + PLACED ( 1000 1000 ) N ;
- redundant INV + PLACED ( 3000 1000 ) N ;
END COMPONENTS
PINS 2 ;
- a + NET na + DIRECTION INPUT + USE SIGNAL + LAYER M1 ( 0 0 ) ( 20 20 ) + FIXED ( 0 1000 ) N ;
- z + NET nz + DIRECTION OUTPUT + USE SIGNAL + LAYER M1 ( 0 0 ) ( 20 20 ) + FIXED ( 10000 1000 ) N ;
END PINS
NETS 2 ;
- na ( PIN a ) ( u A ) ;
- nz ( u Y ) ( PIN z ) ;
END NETS
BLOCKAGES 2 ;
- PLACEMENT RECT ( 0 0 ) ( 1000 1000 ) ;
- PLACEMENT + SOFT + COMPONENT u RECT ( 1000 0 ) ( 2000 1000 ) ;
END BLOCKAGES
END DESIGN
""")
        liberty = root / "cells.lib"
        liberty.write_text("""library(test){time_unit:"1ps";capacitive_load_unit(1,ff);
cell(INV){pin(A){direction:input;capacitance:2;}
pin(Y){direction:output;function:"!A";timing(){related_pin:A;timing_sense:negative_unate;
cell_rise(scalar){values("10");}cell_fall(scalar){values("20");}
rise_transition(scalar){values("3");}fall_transition(scalar){values("4");}
}}}}
""")
        output = root / "timing.json"
        config = root / "config.json"
        config.write_text(json.dumps({
            "liberty": [str(liberty)], "lefs": [str(lef)], "def": str(netlist),
            "output": str(output), "constraints_complete": True,
            "pins": ["z"], "include_predecessors": True,
            "inputs": {"a": {"arrival_ps": [1, 2], "slew_ps": [3, 4]}},
            "outputs": {"z": {"required_ps": [100, 90], "load_ff": [5, 6]}},
        }))
        subprocess.run([binary, config], check=True)
        result = json.loads(output.read_text())
        assert result["certified"], result["unsupported"]
        assert result["signal_instances"] == 2  # redundant cell must survive
        assert result["placement_blockages"] == 2
        assert result["placement_blockage_rectangles"] == 2
        assert result["wire_rc"] == "zero"
        assert result["wns_ps"] == 69
        assert result["pins"]["z"]["transitions"][0]["arrival_ps"] == 12
        assert result["pins"]["z"]["transitions"][1]["arrival_ps"] == 21
        assert result["predecessor_paths_included"]
        assert set(result["pins"]) == {"z", "u:Y", "u:A", "a"}
        assert subprocess.run([binary, config], capture_output=True).returncode != 0
        check_named_clocks(root, binary, lef)


if __name__ == "__main__":
    main()
