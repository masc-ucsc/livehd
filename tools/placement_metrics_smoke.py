# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Check physical wirelength and reset-cut policy on hand-computed geometry."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    binary = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(dir=os.environ.get("TEST_TMPDIR")) as directory:
        root = Path(directory)
        lef = ["VERSION 5.8 ; UNITS DATABASE MICRONS 1000 ; END UNITS",
               "SITE core CLASS CORE ; SIZE 1 BY 2 ; END core",
               "LAYER M1 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.1 ; WIDTH 0.05 ; END M1",
               "LAYER M2 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.1 ; WIDTH 0.05 ; END M2"]
        for name, pins in {
            "BUF": {"A": (0.5, "INPUT"), "Y": (1.5, "OUTPUT")},
            "REG": {"D": (0.5, "INPUT"), "Q": (1.5, "OUTPUT"),
                    "C": (1.0, "INPUT"), "R": (1.0, "INPUT")},
        }.items():
            lef.append(f"MACRO {name} CLASS CORE ; ORIGIN 0 0 ; SIZE 2 BY 2 ;")
            for pin, (x, direction) in pins.items():
                use = "CLOCK" if pin == "C" else "SIGNAL"
                lef.append(f"PIN {pin} DIRECTION {direction} ; USE {use} ; "
                           f"PORT LAYER M1 ; RECT {x-.1} 0.9 {x+.1} 1.1 ; END END {pin}")
            lef.append(f"END {name}")
        lef.extend(["MACRO BLOCK CLASS BLOCK ; ORIGIN 0 0 ; SIZE 4 BY 4 ; END BLOCK",
                    "MACRO FILL CLASS CORE SPACER ; ORIGIN 0 0 ; SIZE 1 BY 2 ; SITE core ; END FILL",
                    "END LIBRARY"])
        (root / "tech.lef").write_text("\n".join(lef))
        (root / "model.lib").write_text('''library(test) {
          cell(BUF) { area: 4;
            pin(A) { direction: input; }
            pin(Y) { direction: output; function: "A"; }
          }
          cell(REG) { area: 4;
            ff(IQ, IQN) { clocked_on: "C"; next_state: "D"; clear: "!R"; }
            pin(C) { direction: input; clock: true; }
            pin(R) { direction: input; }
            pin(D) { direction: input; }
            pin(Q) { direction: output; function: "IQ"; }
          }
        }''')
        definition = ["VERSION 5.8 ; DESIGN check ; UNITS DISTANCE MICRONS 1000 ;",
                      "DIEAREA ( 0 0 ) ( 50000 30000 ) ;"]
        definition.extend(f"ROW r{y} core 0 {y} N DO 50 BY 1 STEP 1000 0 ;"
                          for y in range(0, 30000, 2000))
        definition.append("COMPONENTS 4 ;")
        for name, master, x, y in [("clkbuf", "BUF", 10, 0), ("rstbuf", "BUF", 10, 10),
                                    ("databuf", "BUF", 10, 20), ("reg", "REG", 30, 0)]:
            definition.append(f"- {name} {master} + PLACED ( {x*1000} {y*1000} ) N ;")
        definition.extend(["END COMPONENTS", "PINS 4 ;"])
        for name, net, x, y in [("CLK", "c0", 0, 0), ("RST", "r0", 0, 10),
                                 ("IN", "d0", 0, 20), ("OUT", "d2", 40, 20)]:
            definition.append(f"- {name} + NET {net} + DIRECTION INPUT "
                              f"+ FIXED ( {x*1000} {y*1000} ) N ;")
        definition.extend(["END PINS", "NETS 7 ;",
                           "- c0 ( PIN CLK ) ( clkbuf A ) ;",
                           "- c1 ( clkbuf Y ) ( reg C ) ;",
                           "- r0 ( PIN RST ) ( rstbuf A ) ;",
                           "- r1 ( rstbuf Y ) ( reg R ) ;",
                           "- d0 ( PIN IN ) ( databuf A ) ;",
                           "- d1 ( databuf Y ) ( reg D ) ;",
                           "- d2 ( reg Q ) ( PIN OUT ) ;", "END NETS", "END DESIGN"])
        (root / "placed.def").write_text("\n".join(definition))
        (root / "controls.json").write_text(json.dumps({
            "hpwl_um": 78,
            "prects_controls": {"ports": ["CLK", "RST"], "reset_ports": ["RST"],
                                "pins": [], "reset_pins": [], "override": False,
                                "ignored_nets": 4},
        }))
        command = [binary, "--lef", str(root / "tech.lef"), "--def", str(root / "placed.def"),
                   "--liberty", str(root / "model.lib"), "--controls", str(root / "controls.json"),
                   "--output", str(root / "metrics.json"), "--check-native",
                   "--snapshot", str(root / "snapshot.txt"), "--uroute-geometry", str(root / "geometry.json")]
        subprocess.run(command, check=True)
        result = json.loads((root / "metrics.json").read_text())
        for name, expected in [("all_hpwl_um", 150), ("clock_excluded_hpwl_um", 119),
                               ("data_hpwl_um", 78), ("native_data_hpwl_difference_um", 0)]:
            assert abs(result[name] - expected) < 1e-9, (name, result[name], expected)
        assert set(result["clock_nets"]) == {"c0", "c1"}
        geometry = json.loads((root / "geometry.json").read_text())
        assert geometry["core"] == [0, 0, 50000, 30000], geometry
        assert {n["name"] for n in geometry["nets"] if n["enabled"]} == {"d0", "d1", "d2"}
        assert geometry["nets"][0]["pins"] == [[0, 0], [10500, 1000]], geometry["nets"][0]
        assert set(result["reset_cut_nets"]) == {"r0", "r1"}
        assert "displacement" not in result
        # Compare legalization snapshots using origins, independently of net
        # names and clock/reset HPWL cuts. Include a fixed macro and a filler.
        after = "\n".join(definition).replace("COMPONENTS 4 ;", "COMPONENTS 6 ;").replace(
            "END COMPONENTS", "- mem BLOCK + FIXED ( 44000 24000 ) N ;\n"
            "- fill FILL + PLACED ( 1000 2000 ) N ;\nEND COMPONENTS")
        before = after.replace("clkbuf BUF + PLACED ( 10000 0 ) N", "clkbuf BUF + FIXED ( 7000 -4000 ) N").replace(
            "reg REG + PLACED ( 30000 0 ) N", "reg REG + PLACED ( 30000 0 ) S").replace(
            "mem BLOCK + FIXED ( 44000 24000 ) N", "mem BLOCK + FIXED ( 45000 25000 ) N").replace(
            "fill FILL + PLACED ( 1000 2000 ) N", "fill FILL + PLACED ( 0 0 ) N")
        (root / "after.def").write_text(after)
        (root / "before.def").write_text(before)
        comparison = command.copy()
        comparison[comparison.index("--def") + 1] = str(root / "after.def")
        comparison += ["--reference-def", str(root / "before.def")]
        subprocess.run(comparison, check=True)
        movement = json.loads((root / "metrics.json").read_text())["displacement"]
        expected = {"all_instances": (6, 3, 12., 2., 7., 2),
                    "standard_cells": (4, 1, 7., 1.75, 7., 1),
                    "macros": (1, 1, 2., 2., 2., 1),
                    "physical_only": (1, 1, 3., 3., 3., 0)}
        for category, values in expected.items():
            observed = movement[category]
            assert tuple(observed[k] for k in ("instances", "moved", "total_um", "mean_um", "maximum_um",
                                               "fixed_instances_moved")) == values, observed
        assert movement["all_instances"]["orientation_changes"] == 1
        assert movement["all_instances"]["status_changes"] == 1
        subprocess.run(command + ["--reference-def", str(root / "placed.def")], check=True)
        identical = json.loads((root / "metrics.json").read_text())["displacement"]
        assert identical["all_instances"]["moved"] == identical["all_instances"]["total_um"] == 0
        assert identical["macros"]["instances"] == 0 and identical["macros"]["mean_um"] is None
        assert identical["physical_only"]["maximum_um"] is None
        for changed, message in [
                (before.replace("DISTANCE MICRONS 1000", "DISTANCE MICRONS 2000"), "matching DEF design and units"),
                (before.replace("DESIGN check", "DESIGN other"), "matching DEF design and units"),
                (before.replace("- mem BLOCK", "- missing BLOCK"), "instance/master differs"),
                (before.replace("- fill FILL", "- mem FILL"), "instance/master differs"),
                (before.replace("clkbuf BUF", "clkbuf REG"), "instance/master differs"),
                (before.replace("- fill FILL + PLACED ( 0 0 ) N ;", "").replace("COMPONENTS 6", "COMPONENTS 5"),
                 "instance census differs"),
                (before.replace("clkbuf BUF + FIXED ( 7000 -4000 ) N", "clkbuf BUF + UNPLACED"), "placed instances")]:
            (root / "before.def").write_text(changed)
            failed = subprocess.run(comparison, capture_output=True, text=True)
            assert failed.returncode == 1 and message in failed.stderr, failed.stderr
        # DEF row order does not define the core bounds. In particular, a
        # reversed row list must produce the same physical routing domain.
        rows = [line for line in definition if line.startswith("ROW ")]
        shuffled = iter(reversed(rows))
        (root / "placed.def").write_text("\n".join(
            next(shuffled) if line.startswith("ROW ") else line
            for line in definition))
        subprocess.run(command, check=True)
        domain = list(map(float, (root / "snapshot.txt").read_text().splitlines()[0].split()))
        assert domain[:4] == [0, 0, 50000, 30000], domain
        for model in ("rudy", "rsmt"):
            prefix = root / model
            subprocess.run([str(Path(sys.argv[2]).resolve()), str(root / "snapshot.txt"),
                            str(prefix), model], check=True)
            route = json.loads(prefix.with_suffix(".json").read_text())
            assert route["nets"] == 5 and route["nodes"] == 8, route
            assert abs(route["hpwl_dbu"] - 119000) < 1e-6, route["hpwl_dbu"]
        controls = json.loads((root / "controls.json").read_text())
        for wrong, message in [("hpwl", "data HPWL differs"),
                               ("census", "control-net census differs"),
                               ("override", "not export enough clock-role metadata")]:
            modified = json.loads(json.dumps(controls))
            if wrong == "hpwl":
                modified["hpwl_um"] = 79
            elif wrong == "census":
                modified["prects_controls"]["ignored_nets"] = 3
            else:
                modified["prects_controls"]["override"] = True
            (root / "controls.json").write_text(json.dumps(modified))
            failed = subprocess.run(command, capture_output=True, text=True)
            assert failed.returncode != 0 and message in failed.stderr, failed.stderr
        for key, value, message in [
                ("prects_controls", None, "prects_controls must be an object"),
                ("ports", "CLK", "ports must be an array of strings"),
                ("pins", [42], "pins must be an array of strings"),
                ("override", 1, "override must be a boolean"),
                ("hpwl_um", "78", "hpwl_um must be a number")]:
            modified = json.loads(json.dumps(controls))
            if key in ("prects_controls", "hpwl_um"):
                modified[key] = value
            else:
                modified["prects_controls"][key] = value
            (root / "controls.json").write_text(json.dumps(modified))
            failed = subprocess.run(command, capture_output=True, text=True)
            assert failed.returncode == 1 and message in failed.stderr, failed.stderr


if __name__ == "__main__":
    main()
