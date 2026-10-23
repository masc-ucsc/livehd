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
        lef.append("END LIBRARY")
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
                   "--snapshot", str(root / "snapshot.txt")]
        subprocess.run(command, check=True)
        result = json.loads((root / "metrics.json").read_text())
        for name, expected in [("all_hpwl_um", 150), ("clock_excluded_hpwl_um", 119),
                               ("data_hpwl_um", 78), ("native_data_hpwl_difference_um", 0)]:
            assert abs(result[name] - expected) < 1e-9, (name, result[name], expected)
        assert set(result["clock_nets"]) == {"c0", "c1"}
        assert set(result["reset_cut_nets"]) == {"r0", "r1"}
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
