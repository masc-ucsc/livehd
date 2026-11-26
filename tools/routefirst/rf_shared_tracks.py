# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
"""Self-contained ASAP7 shared-track multi-metal (M1/M2/M3) mux-crossbar generator.

Reproduces the multi-metal pin-escape bottleneck seen in NVDLA/partition_o:
- 54 nm site, 270 nm row height (alternating N and FS), 1000 DBU/um
- Non-uniform ASAP7 M2 tracks per row: followpin rail at 0, signal tracks at {45, 81, 117, 153, 189, 225}
- M3 vertical pitch 36 nm (so alternate 54 nm gate columns sit off-M3 and must jog +/-18 nm on M2)
- AO222x2 (4 consecutive 54 nm inner pins restricted to {81, 117, 153}), AO221x1 (5 consecutive 54 nm pins on
  {81, 117, 153}), AO32x1, OR4x1, BUFx2, and DFFHQNx1 cells
- Multi-sink select nets hitting inner shared-track pins, vertical crossing nets, and critical mux-to-mux data nets
"""

import argparse
import pathlib
import random

ASAP7_TECH_LEF = """VERSION 5.8 ;
BUSBITCHARS "[]" ;
DIVIDERCHAR "/" ;
UNITS DATABASE MICRONS 1000 ; END UNITS
SITE asap7sc7p5t CLASS CORE ; SIZE 0.054 BY 0.270 ; END asap7sc7p5t
LAYER M1 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.036 ; WIDTH 0.018 ; END M1
LAYER V1 TYPE CUT ; END V1
LAYER M2 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.036 ; WIDTH 0.018 ; END M2
LAYER V2 TYPE CUT ; END V2
LAYER M3 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.036 ; WIDTH 0.018 ; END M3
LAYER V3 TYPE CUT ; END V3
LAYER M4 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.048 ; WIDTH 0.024 ; END M4
LAYER V4 TYPE CUT ; END V4
LAYER M5 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.048 ; WIDTH 0.024 ; END M5
LAYER V5 TYPE CUT ; END V5
LAYER M6 TYPE ROUTING ; DIRECTION HORIZONTAL ; PITCH 0.064 ; WIDTH 0.032 ; END M6
LAYER V6 TYPE CUT ; END V6
LAYER M7 TYPE ROUTING ; DIRECTION VERTICAL ; PITCH 0.064 ; WIDTH 0.032 ; END M7
VIA VIA12 DEFAULT
 LAYER M1 ; RECT -0.009 -0.011 0.009 0.011 ;
 LAYER V1 ; RECT -0.009 -0.009 0.009 0.009 ;
 LAYER M2 ; RECT -0.019 -0.009 0.019 0.009 ;
END VIA12
VIA VIA23 DEFAULT
 LAYER M2 ; RECT -0.019 -0.009 0.019 0.009 ;
 LAYER V2 ; RECT -0.009 -0.009 0.009 0.009 ;
 LAYER M3 ; RECT -0.009 -0.019 0.009 0.019 ;
END VIA23
VIA VIA34 DEFAULT
 LAYER M3 ; RECT -0.009 -0.019 0.009 0.019 ;
 LAYER V3 ; RECT -0.009 -0.012 0.009 0.012 ;
 LAYER M4 ; RECT -0.023 -0.012 0.023 0.012 ;
END VIA34
VIA VIA45 DEFAULT
 LAYER M4 ; RECT -0.023 -0.012 0.023 0.012 ;
 LAYER V4 ; RECT -0.012 -0.012 0.012 0.012 ;
 LAYER M5 ; RECT -0.012 -0.027 0.012 0.027 ;
END VIA45
VIA VIA56 DEFAULT
 LAYER M5 ; RECT -0.012 -0.027 0.012 0.027 ;
 LAYER V5 ; RECT -0.012 -0.016 0.012 0.016 ;
 LAYER M6 ; RECT -0.027 -0.016 0.027 0.016 ;
END VIA56
VIA VIA67 DEFAULT
 LAYER M6 ; RECT -0.027 -0.016 0.027 0.016 ;
 LAYER V6 ; RECT -0.016 -0.016 0.016 0.016 ;
 LAYER M7 ; RECT -0.016 -0.027 0.016 0.027 ;
END VIA67
END LIBRARY
"""

ASAP7_CELLS_LEF = """VERSION 5.8 ;
MACRO AO222x2
 CLASS CORE ; ORIGIN 0 0 ; SIZE 0.648 BY 0.270 ; SYMMETRY X Y ; SITE asap7sc7p5t ;
 PIN C1 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.072 0.070 0.090 0.164 ; END END C1
 PIN C2 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.126 0.070 0.144 0.164 ; END END C2
 PIN B2 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.180 0.070 0.198 0.164 ; END END B2
 PIN B1 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.234 0.070 0.252 0.164 ; END END B1
 PIN A1 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.396 0.070 0.414 0.200 ; END END A1
 PIN A2 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.450 0.070 0.468 0.200 ; END END A2
 PIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ;
  RECT 0.526 0.225 0.630 0.243 ; RECT 0.612 0.027 0.630 0.243 ; RECT 0.531 0.027 0.630 0.045 ;
 END END Y
 PIN VDD DIRECTION INOUT ; USE POWER ; PORT LAYER M1 ; RECT 0 0.261 0.648 0.279 ; END END VDD
 PIN VSS DIRECTION INOUT ; USE GROUND ; PORT LAYER M1 ; RECT 0 -0.009 0.648 0.009 ; END END VSS
END AO222x2

MACRO AO221x1
 CLASS CORE ; ORIGIN 0 0 ; SIZE 0.540 BY 0.270 ; SYMMETRY X Y ; SITE asap7sc7p5t ;
 PIN B1 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.072 0.070 0.090 0.164 ; END END B1
 PIN B2 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.126 0.070 0.144 0.164 ; END END B2
 PIN C  DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.180 0.070 0.198 0.164 ; END END C
 PIN A1 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.234 0.070 0.252 0.164 ; END END A1
 PIN A2 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.288 0.070 0.306 0.164 ; END END A2
 PIN Y  DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ;
  RECT 0.472 0.225 0.522 0.243 ; RECT 0.504 0.027 0.522 0.243 ; RECT 0.459 0.027 0.522 0.045 ;
 END END Y
 PIN VDD DIRECTION INOUT ; USE POWER ; PORT LAYER M1 ; RECT 0 0.261 0.540 0.279 ; END END VDD
 PIN VSS DIRECTION INOUT ; USE GROUND ; PORT LAYER M1 ; RECT 0 -0.009 0.540 0.009 ; END END VSS
END AO221x1

MACRO AO32x1
 CLASS CORE ; ORIGIN 0 0 ; SIZE 0.432 BY 0.270 ; SYMMETRY X Y ; SITE asap7sc7p5t ;
 PIN A3 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.126 0.070 0.144 0.200 ; END END A3
 PIN A2 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.180 0.070 0.198 0.200 ; END END A2
 PIN A1 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.234 0.070 0.252 0.200 ; END END A1
 PIN B1 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.288 0.070 0.306 0.164 ; END END B1
 PIN B2 DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.342 0.070 0.360 0.164 ; END END B2
 PIN Y  DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ;
  RECT 0.018 0.225 0.068 0.243 ; RECT 0.018 0.063 0.063 0.081 ; RECT 0.018 0.063 0.036 0.243 ;
 END END Y
 PIN VDD DIRECTION INOUT ; USE POWER ; PORT LAYER M1 ; RECT 0 0.261 0.432 0.279 ; END END VDD
 PIN VSS DIRECTION INOUT ; USE GROUND ; PORT LAYER M1 ; RECT 0 -0.009 0.432 0.009 ; END END VSS
END AO32x1

MACRO OR4x1
 CLASS CORE ; ORIGIN 0 0 ; SIZE 0.378 BY 0.270 ; SYMMETRY X Y ; SITE asap7sc7p5t ;
 PIN D DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.126 0.106 0.144 0.236 ; END END D
 PIN C DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.180 0.070 0.198 0.236 ; END END C
 PIN B DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.234 0.070 0.252 0.236 ; END END B
 PIN A DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.288 0.070 0.306 0.200 ; END END A
 PIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ;
  RECT 0.018 0.225 0.068 0.243 ; RECT 0.018 0.027 0.068 0.045 ; RECT 0.018 0.027 0.036 0.243 ;
 END END Y
 PIN VDD DIRECTION INOUT ; USE POWER ; PORT LAYER M1 ; RECT 0 0.261 0.378 0.279 ; END END VDD
 PIN VSS DIRECTION INOUT ; USE GROUND ; PORT LAYER M1 ; RECT 0 -0.009 0.378 0.009 ; END END VSS
END OR4x1

MACRO BUFx2
 CLASS CORE ; ORIGIN 0 0 ; SIZE 0.162 BY 0.270 ; SYMMETRY X Y ; SITE asap7sc7p5t ;
 PIN A DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.018 0.070 0.036 0.200 ; END END A
 PIN Y DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.108 0.036 0.126 0.234 ; END END Y
 PIN VDD DIRECTION INOUT ; USE POWER ; PORT LAYER M1 ; RECT 0 0.261 0.162 0.279 ; END END VDD
 PIN VSS DIRECTION INOUT ; USE GROUND ; PORT LAYER M1 ; RECT 0 -0.009 0.162 0.009 ; END END VSS
END BUFx2

MACRO DFFHQNx1
 CLASS CORE ; ORIGIN 0 0 ; SIZE 0.432 BY 0.270 ; SYMMETRY X Y ; SITE asap7sc7p5t ;
 PIN CLK DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.072 0.070 0.090 0.200 ; END END CLK
 PIN D   DIRECTION INPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.180 0.070 0.198 0.200 ; END END D
 PIN QN  DIRECTION OUTPUT ; USE SIGNAL ; PORT LAYER M1 ; RECT 0.360 0.036 0.378 0.234 ; END END QN
 PIN VDD DIRECTION INOUT ; USE POWER ; PORT LAYER M1 ; RECT 0 0.261 0.432 0.279 ; END END VDD
 PIN VSS DIRECTION INOUT ; USE GROUND ; PORT LAYER M1 ; RECT 0 -0.009 0.432 0.009 ; END END VSS
END DFFHQNx1
END LIBRARY
"""

WIDTH = {"AO222x2": 648, "AO221x1": 540, "AO32x1": 432, "OR4x1": 378, "BUFx2": 162, "DFFHQNx1": 432}


def build_shared_track_bundle(out_dir: pathlib.Path, *, bits: int = 8, prelegal: bool = False, seed: int = 11):
    """Write a self-contained DEF/LEF/timing bundle modeling a 2-stage shared-track mux crossbar."""
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "tech.lef").write_text(ASAP7_TECH_LEF)
    (out_dir / "cells.lef").write_text(ASAP7_CELLS_LEF)
    (out_dir / "lefs.json").write_text('["tech.lef","cells.lef"]\n')
    rng = random.Random(seed)

    # Scale die and row geometry smoothly with `bits` (8 for fast unit tests, 32 for full stress benchmark).
    n_rows = 8 if bits <= 8 else 12
    sites_per_row = 120 if bits <= 8 else 220
    x0, y0 = 540, 540
    die_w = x0 * 2 + sites_per_row * 54
    die_h = y0 * 2 + n_rows * 270
    rx1 = x0 + sites_per_row * 54
    ry1 = y0 + n_rows * 270

    rows = [
        f"ROW ROW_{r} asap7sc7p5t {x0} {y0 + r * 270} {'N' if r % 2 == 0 else 'FS'} DO {sites_per_row} BY 1 STEP 54 0 ;"
        for r in range(n_rows)
    ]
    tracks = [
        f"TRACKS X 9 DO {die_w // 36} STEP 36 LAYER M1 ;",
        f"TRACKS Y 9 DO {die_h // 36} STEP 36 LAYER M1 ;",
    ]
    for off in (45, 81, 117, 153, 189, 225, 270):
        tracks.append(f"TRACKS X 9 DO {die_w // 36} STEP 36 LAYER M2 ;")
        tracks.append(f"TRACKS Y {off} DO {die_h // 270} STEP 270 LAYER M2 ;")
    tracks += [
        f"TRACKS X 9 DO {die_w // 36} STEP 36 LAYER M3 ;",
        f"TRACKS Y 9 DO {die_h // 36} STEP 36 LAYER M3 ;",
        f"TRACKS X 45 DO {die_w // 36} STEP 36 LAYER M4 ;",
        f"TRACKS Y 12 DO {die_h // 48} STEP 48 LAYER M4 ;",
        f"TRACKS X 12 DO {die_w // 48} STEP 48 LAYER M5 ;",
        f"TRACKS Y 12 DO {die_h // 48} STEP 48 LAYER M5 ;",
        f"TRACKS X 60 DO {die_w // 48} STEP 48 LAYER M6 ;",
        f"TRACKS Y 16 DO {die_h // 64} STEP 64 LAYER M6 ;",
        f"TRACKS X 16 DO {die_w // 64} STEP 64 LAYER M7 ;",
        f"TRACKS Y 16 DO {die_h // 64} STEP 64 LAYER M7 ;",
    ]

    vdd, vss = [], []
    for r in range(n_rows + 1):
        (vss if r % 2 == 0 else vdd).append(f"M2 18 + SHAPE FOLLOWPIN ( {x0} {y0 + r * 270} ) ( {rx1} * )")
    for sx in range(x0 + 300, rx1 - 300, 5400):
        vdd.append(f"M5 120 + SHAPE STRIPE ( {sx + 60} {y0} ) ( * {ry1} )")
        vss.append(f"M5 120 + SHAPE STRIPE ( {sx + 252} {y0} ) ( * {ry1} )")

    # Build 2-stage mux crossbar: per bit, 3 stage-1 muxes (AO222x2, AO221x1, AO32x1) -> 1 stage-2 mux (AO222x2/OR4x1) -> DFFHQNx1
    stage1_masters = ("AO222x2", "AO221x1", "AO32x1")
    stage2_masters = ("AO222x2", "OR4x1")
    logical_cells = [("clk_buf", "BUFx2", True)]
    sel_bufs = []
    for g in range(3):
        for s in range(3):
            bname = f"sel_buf_{g}_{s}"
            sel_bufs.append((g, s, bname))
            logical_cells.append((bname, "BUFx2", True))

    stg1_by_bit = []
    stg2_by_bit = []
    flops = []
    for b in range(bits):
        s1 = []
        for j, m in enumerate(stage1_masters):
            cname = f"u_s1_{b}_{j}_{m}"
            s1.append((cname, m))
            logical_cells.append((cname, m, False))
        stg1_by_bit.append(s1)
        m2 = stage2_masters[b % 2]
        s2_name = f"u_s2_{b}_{m2}"
        stg2_by_bit.append((s2_name, m2))
        logical_cells.append((s2_name, m2, False))
        fname = f"u_ff_{b}"
        flops.append(fname)
        logical_cells.append((fname, "DFFHQNx1", True))

    # Place fixed anchors first on top/bottom rows, and pack movable shared-track muxes into dense middle rows.
    placed = []
    anchor_row_x = {0: x0, n_rows - 1: x0}
    mux_rows = list(range(1, n_rows - 1))
    mux_cur = {r: x0 + 216 for r in mux_rows}
    r_cursor = 0
    for cname, master, fixed in logical_cells:
        w = WIDTH[master]
        if fixed:
            r = 0 if cname.startswith(("clk", "sel")) else (n_rows - 1 if anchor_row_x[n_rows - 1] + w <= rx1 else 0)
            px = anchor_row_x[r]
            py = y0 + r * 270
            anchor_row_x[r] += w + 108
            placed.append((cname, master, px, py, "N" if r % 2 == 0 else "FS", True))
        else:
            for _ in range(len(mux_rows)):
                r = mux_rows[r_cursor % len(mux_rows)]
                r_cursor += 1
                if mux_cur[r] + w <= rx1 - 216:
                    break
            ori = "N" if r % 2 == 0 else "FS"
            if prelegal:
                px = x0 + sites_per_row * 27 + rng.randint(-900, 900)
                py = y0 + (n_rows // 2 + rng.randint(-1, 1)) * 270
            else:
                px = mux_cur[r]
                py = y0 + r * 270
            mux_cur[r] += w + 54  # 1-site spacing inside mux rows
            placed.append((cname, master, px, py, ori, False))

    pins = []
    nets = []
    slacks = []
    nets.append("- clk ( clk_buf Y ) " + " ".join(f"( {f} CLK )" for f in flops) + " + USE CLOCK ;")

    # Select nets fan out across all bits onto the inner shared-track pins of stage-1/stage-2 muxes.
    sel_conns = {(g, s): [f"( sel_buf_{g}_{s} Y )"] for g in range(3) for s in range(3)}
    data_inputs = []
    for b in range(bits):
        # Stage 1: AO222x2 (sel on C1,B1,A1; data on C2,B2,A2), AO221x1 (sel on B1,A1; data on B2,C,A2), AO32x1 (sel on A1,B1; data on A3,A2,B2)
        c0, _ = stg1_by_bit[b][0]
        c1, _ = stg1_by_bit[b][1]
        c2, _ = stg1_by_bit[b][2]
        sel_conns[(0, 0)].append(f"( {c0} C1 )")
        sel_conns[(0, 1)].append(f"( {c0} B1 )")
        sel_conns[(0, 2)].append(f"( {c0} A1 )")
        data_inputs.extend([(c0, "C2"), (c0, "B2"), (c0, "A2")])

        sel_conns[(1, 0)].append(f"( {c1} B1 )")
        sel_conns[(1, 1)].append(f"( {c1} A1 )")
        sel_conns[(1, 2)].append(f"( {c1} C )")
        data_inputs.extend([(c1, "B2"), (c1, "A2")])

        sel_conns[(2, 0)].append(f"( {c2} A1 )")
        sel_conns[(2, 1)].append(f"( {c2} B1 )")
        data_inputs.extend([(c2, "A3"), (c2, "A2"), (c2, "B2")])

        s2_name, s2_m = stg2_by_bit[b]
        if s2_m == "AO222x2":
            sel_conns[(0, 2)].append(f"( {s2_name} C1 )")
            sel_conns[(1, 2)].append(f"( {s2_name} B1 )")
            sel_conns[(2, 2)].append(f"( {s2_name} A1 )")
            s2_d = ("C2", "B2", "A2")
        else:
            sel_conns[(2, 2)].append(f"( {s2_name} D )")
            s2_d = ("C", "B", "A")
        for j in range(3):
            src_c, _ = stg1_by_bit[(b + j) % bits][j]
            nets.append(f"- stg1_{b}_{j} ( {src_c} Y ) ( {s2_name} {s2_d[j]} ) ;")
            slacks.append(f'"{src_c}:Y" {-160 + (b * 3 + j) * 4} 0')
        nets.append(f"- stg2_{b} ( {s2_name} Y ) ( {flops[b]} D ) ;")
        slacks.append(f'"{s2_name}:Y" {-220 + b * 3} 0')

        # Output port on M4 track at top edge
        m4_y = ((die_h - 120 - 12) // 48) * 48 + 12
        px = x0 + 180 + b * 288
        pname = f"dout_{b}"
        pins.append(f"- {pname} + NET {pname} + DIRECTION OUTPUT + LAYER M4 ( -45 -12 ) ( 45 12 ) + FIXED ( {px} {m4_y} ) N ;")
        nets.append(f"- {pname} ( {flops[b]} QN ) ( PIN {pname} ) ;")

    for (g, s), conns in sel_conns.items():
        if len(conns) >= 2:
            nets.append(f"- sel_{g}_{s} " + " ".join(conns) + " ;")
            slacks.append(f'"sel_buf_{g}_{s}:Y" {80 + g * 20 + s * 5} 0')

    # Feed data inputs from bottom M3 ports (each driving 2-3 mux pins across bits to create horizontal + vertical crossings)
    rng.shuffle(data_inputs)
    port_idx = 0
    cursor = 0
    while cursor < len(data_inputs):
        deg = 3 if port_idx % 2 == 0 else 2
        group = data_inputs[cursor : cursor + deg]
        cursor += deg
        # Snap x to M3 grid (9 + 36 * k)
        m3_x = 9 + 36 * (16 + port_idx * 3)
        pname = f"din_{port_idx}"
        pins.append(f"- {pname} + NET {pname} + DIRECTION INPUT + LAYER M3 ( -9 -25 ) ( 9 25 ) + FIXED ( {m3_x} 180 ) N ;")
        nets.append(f"- {pname} ( PIN {pname} ) " + " ".join(f"( {cn} {cp} )" for cn, cp in group) + " ;")
        slacks.append(f'"{pname}" {-40 + port_idx * 5} 0')
        port_idx += 1

    comp_lines = [
        f"- {n} {m} + {'FIXED' if f else 'PLACED'} ( {x} {y} ) {ori} ;"
        for n, m, x, y, ori, f in placed
    ]
    special = (
        "SPECIALNETS 2 ;\n"
        "- VDD ( * VDD ) + USE POWER\n  + FIXED " + "\n    NEW ".join(vdd) + " ;\n"
        "- VSS ( * VSS ) + USE GROUND\n  + FIXED " + "\n    NEW ".join(vss) + " ;\n"
        "END SPECIALNETS\n"
    )
    def_txt = (
        'VERSION 5.8 ;\nDIVIDERCHAR "/" ;\nBUSBITCHARS "[]" ;\nDESIGN shared_track_xbar ;\n'
        f"UNITS DISTANCE MICRONS 1000 ;\nDIEAREA ( 0 0 ) ( {die_w} {die_h} ) ;\n"
        + "\n".join(rows) + "\n"
        + "\n".join(tracks) + "\n"
        + f"COMPONENTS {len(comp_lines)} ;\n" + "\n".join(comp_lines) + "\nEND COMPONENTS\n"
        + f"PINS {len(pins)} ;\n" + "\n".join(pins) + "\nEND PINS\n"
        + special
        + f"NETS {len(nets)} ;\n" + "\n".join(nets) + "\nEND NETS\nEND DESIGN\n"
    )
    (out_dir / "out.def").write_text(def_txt)
    (out_dir / "timing.tsv").write_text("\n".join(slacks) + "\n")
    return len(placed), len(nets)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--out", type=pathlib.Path, required=True, help="Output DEF bundle directory")
    p.add_argument("--bits", type=int, default=32, help="Crossbar width in bits (8 for unit test, 32 for benchmark)")
    p.add_argument("--prelegal", action="store_true", help="Emit unlegalized overlapping cell poses for route-first")
    p.add_argument("--seed", type=int, default=11)
    args = p.parse_args()
    cells, nets = build_shared_track_bundle(args.out, bits=args.bits, prelegal=args.prelegal, seed=args.seed)
    print(f"wrote {args.out}: {cells} cells, {nets} nets (prelegal={args.prelegal})")


if __name__ == "__main__":
    main()
