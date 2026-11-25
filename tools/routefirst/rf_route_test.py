# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
import http.client
import json
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time
import unittest

ROUTER = pathlib.Path(sys.argv.pop(1)).resolve()
CHUNKS = pathlib.Path(sys.argv.pop(1)).resolve()
FEEDBACK = pathlib.Path(sys.argv.pop(1)).resolve()
TECH = 'VERSION 5.8 ;\nBUSBITCHARS "[]" ;\nDIVIDERCHAR "/" ;\n'
TECH += "UNITS DATABASE MICRONS 1000 ; END UNITS\nSITE core CLASS CORE ; SIZE 0.054 BY 0.270 ; END core\n"
TRACKS = []
for number in range(1, 8):
    horizontal = number % 2 == 0
    direction = "HORIZONTAL" if horizontal else "VERTICAL"
    TECH += f"LAYER M{number}\n TYPE ROUTING ; DIRECTION {direction} ; PITCH 0.036 ; WIDTH 0.018 ;\nEND M{number}\n"
    TRACKS.append(f"TRACKS {'Y' if horizontal else 'X'} 18 DO 20 STEP 36 LAYER M{number} ;")
TECH += "END LIBRARY\n"
CELLS = """VERSION 5.8 ;
MACRO BUF
 CLASS CORE ; ORIGIN 0 0 ; SIZE 0.054 BY 0.270 ;
 PIN A
  DIRECTION INPUT ; PORT LAYER M1 ; RECT 0.009 0.001 0.045 0.055 ; END
 END A
 PIN Z
  DIRECTION OUTPUT ; PORT LAYER M1 ; RECT 0.009 0.105 0.045 0.155 ; END
 END Z
END BUF
END LIBRARY
"""


class RouteTest(unittest.TestCase):
    def setUp(self):
        tmp = tempfile.TemporaryDirectory()
        self.addCleanup(tmp.cleanup)
        self.root = pathlib.Path(tmp.name)
        (self.root / "tech.lef").write_text(TECH)
        (self.root / "cells.lef").write_text(CELLS)

    def frame(self, components=(), nets=(), pins=(), tracks=TRACKS, sites=10):
        def section(name, records):
            return f"{name} {len(records)} ;\n" + "\n".join(records) + f"\nEND {name}\n"

        return ("VERSION 5.8 ;\nDIVIDERCHAR \"/\" ;\nBUSBITCHARS \"[]\" ;\nDESIGN tiny ;\n"
                "UNITS DISTANCE MICRONS 1000 ;\nDIEAREA ( 0 0 ) ( 720 720 ) ;\n"
                f"ROW row core 0 270 N DO {sites} BY 1 STEP 54 0 ;\n" + "\n".join(tracks) + "\n"
                + section("COMPONENTS", components) + section("PINS", pins)
                + section("NETS", nets) + "END DESIGN\n")

    def run_router(self, frame, *opts, output="out.def", success=True):
        (self.root / "in.def").write_text(frame)
        run = subprocess.run([ROUTER, *opts, "in.def", output, "tech.lef", "cells.lef"],
                             cwd=self.root, capture_output=True, text=True, timeout=10)
        if success:
            self.assertEqual(run.returncode, 0, run.stderr)
            return json.loads(run.stdout)
        self.assertGreater(run.returncode, 0, run.stderr)
        return run.stderr

    def test_wired_endpoint_and_clock_driver_keep_sites(self):
        for clock in (False, True):
            with self.subTest(clock=clock):
                cells = ["- other BUF + PLACED ( 100 270 ) N ;",
                         "- anchor BUF + PLACED ( 108 270 ) N ;",
                         "- sink BUF + FIXED ( 270 270 ) N ;"]
                wire = "+ USE CLOCK" if clock else "+ FIXED M2 ( 126 414 ) ( 306 * )"
                frame = self.frame(cells, [f"- n ( anchor Z ) ( sink A ) {wire} ;"])
                self.run_router(frame, "--prelegal", "--limit", "0")
                out = (self.root / "out.def").read_text()
                self.assertIn("anchor BUF + PLACED ( 108 270 ) N", out)
                self.assertIn("other BUF + PLACED ( 54 270 ) N", out)

    def test_clock_list_and_off_row_driver(self):
        (self.root / "clock.txt").write_text("clock\n")
        frame = self.frame(["- buf BUF + PLACED ( 109 272 ) N ;", "- sink BUF + FIXED ( 270 270 ) N ;"],
                           ["- clock ( buf Z ) ( sink A ) ;"])
        stats = self.run_router(frame, "--clock-nets", "clock.txt", "--prelegal", "--limit", "0", "--verify")
        self.assertEqual(stats["verify_cell_off_site"], 0)
        self.assertEqual(stats["cells_settled_up_front"], 1)
        (self.root / "clock.txt").write_text("buf/Z\n")  # an endpoint names the same net whatever the net is called
        stats = self.run_router(frame, "--clock-nets", "clock.txt", "--prelegal", "--limit", "0", "--verify")
        self.assertEqual(stats["cells_settled_up_front"], 1)
        (self.root / "clock.txt").write_text("stale_name\n")
        self.assertIn("stale_name", self.run_router(frame, "--clock-nets", "clock.txt", success=False))

    def test_prelegal_sink_overlaps_fixed_cell(self):
        cells = ["- buf BUF + FIXED ( 270 270 ) N ;", "- immovable BUF + FIXED ( 54 270 ) N ;",
                 "- sink BUF + PLACED ( 54 270 ) N ;"]
        frame = self.frame(cells, ["- clk ( buf Z ) ( sink A ) + USE CLOCK ;"])
        stats = self.run_router(frame, "--prelegal", "--limit", "0", "--verify")
        self.assertEqual(stats["verify_cell_overlap"], 0)
        self.assertEqual(stats["cells_settled_up_front"], 1)

    def test_off_row_cell_has_nearest_site(self):
        frame = self.frame(["- buf BUF + PLACED ( 55 1000 ) N ;"])
        stats = self.run_router(frame, "--prelegal", "--verify")
        self.assertEqual(stats["verify_cell_off_site"], 0)
        self.assertEqual(stats["plan_cells_left_out"], 0)

    # ---- rows and exclusions: the final footprint is checked against the original rows and blockages ----
    def placed(self, name):
        found = re.search(rf"- {name} \S+ \+ PLACED \( (\d+) (\d+) \)", (self.root / "out.def").read_text())
        return int(found[1]), int(found[2])

    def test_unsupported_blockage_is_refused(self):
        frame = self.frame().replace("NETS 0", "BLOCKAGES 1 ;\n- LAYER M2 + SPACING 100 RECT ( 0 0 ) ( 54 54 ) ;\nEND BLOCKAGES\nNETS 0")
        self.assertIn("BLOCKAGES", self.run_router(frame, success=False))

    def test_routing_blockage_enters_occupancy(self):
        frame = self.frame().replace("NETS 0", "BLOCKAGES 1 ;\n- LAYER M2 RECT ( 0 0 ) ( 54 54 ) ;\nEND BLOCKAGES\nNETS 0")
        self.assertEqual(self.run_router(frame)["routing_blockage_rects"], 1)

    def test_gap_between_subrows_is_not_a_site(self):
        frame = self.frame(["- c BUF + PLACED ( 108 270 ) N ;"], sites=2)
        frame = frame.replace("TRACKS X 18", "ROW more core 216 270 N DO 2 BY 1 STEP 54 0 ;\nTRACKS X 18", 1)
        stats = self.run_router(frame, "--prelegal", "--verify")
        self.assertEqual(stats["row_stretches_closed_by_split_rows"], 1)
        self.assertEqual((stats["verify_cell_off_site"], stats["verify_cell_overlap"]), (0, 0))
        self.assertIn(self.placed("c"), ((54, 270), (216, 270)))

    def test_placement_blockage_closes_sites(self):
        frame = self.frame(["- c BUF + PLACED ( 108 270 ) N ;"])
        frame = frame.replace("NETS 0", "BLOCKAGES 1 ;\n- PLACEMENT RECT ( 108 270 ) ( 216 540 ) ;\nEND BLOCKAGES\nNETS 0")
        stats = self.run_router(frame, "--prelegal", "--verify")
        self.assertEqual(stats["row_stretches_closed_by_blockages"], 1)
        self.assertEqual((stats["verify_cell_off_site"], stats["verify_cell_overlap"]), (0, 0))
        self.assertIn(self.placed("c"), ((54, 270), (216, 270)))
        # the same pose taken as legal is caught by the check
        self.assertEqual(self.run_router(frame, "--verify")["verify_cell_overlap"], 1)

    def test_macro_spanning_two_rows(self):
        (self.root / "cells.lef").write_text(CELLS.replace(
            "END LIBRARY", "MACRO BLK\n CLASS BLOCK ; ORIGIN 0 0 ; SIZE 0.108 BY 0.540 ;\nEND BLK\nEND LIBRARY"))
        frame = self.frame(["- big BLK + FIXED ( 108 270 ) N ;", "- c BUF + PLACED ( 108 540 ) FS ;"])
        frame = frame.replace("TRACKS X 18", "ROW upper core 0 540 FS DO 10 BY 1 STEP 54 0 ;\nTRACKS X 18", 1)
        stats = self.run_router(frame, "--prelegal", "--verify")
        self.assertEqual(stats["row_stretches_closed_by_macros"], 2)  # its own row and the one above
        self.assertEqual((stats["verify_cell_off_site"], stats["verify_cell_overlap"]), (0, 0))
        self.assertIn(self.placed("c"), ((54, 540), (216, 540)))

    def test_clock_sink_settles_across_a_tap(self):
        cells = ["- l0 BUF + FIXED ( 0 270 ) N ;", "- l1 BUF + FIXED ( 54 270 ) N ;", "- tap BUF + FIXED ( 108 270 ) N ;",
                 "- buf BUF + FIXED ( 378 270 ) N ;", "- sink BUF + PLACED ( 108 270 ) N ;"]
        frame = self.frame(cells, ["- clk ( buf Z ) ( sink A ) + USE CLOCK ;"])
        stats = self.run_router(frame, "--prelegal", "--limit", "0", "--verify")
        self.assertEqual((stats["verify_cell_off_site"], stats["verify_cell_overlap"]), (0, 0))
        self.assertEqual(self.placed("sink"), (162, 270))

    def test_rotated_placed_macro_stays_at_its_pose(self):
        block = "MACRO BLK\n CLASS BLOCK ; ORIGIN 0 0 ; SIZE 0.324 BY 0.054 ;\nEND BLK\nEND LIBRARY"
        (self.root / "cells.lef").write_text(CELLS.replace("END LIBRARY", block))
        cells = ["- block BLK + PLACED ( 216 270 ) E ;", "- beside BUF + FIXED ( 324 270 ) N ;",
                 "- overlap BUF + PLACED ( 216 540 ) N ;"]
        frame = self.frame(cells).replace("TRACKS X 18", "ROW upper core 0 540 N DO 10 BY 1 STEP 54 0 ;\nTRACKS X 18", 1)
        stats = self.run_router(frame, "--prelegal", "--verify")
        self.assertEqual(stats["verify_cell_overlap"], 0)
        self.assertEqual(self.placed("block"), (216, 270))
        self.assertNotEqual(self.placed("overlap"), (216, 540))

    def test_no_available_row(self):
        cells = ["- fixed BUF + FIXED ( 0 270 ) N ;", "- extra BUF + PLACED ( 0 270 ) N ;"]
        stats = self.run_router(self.frame(cells, sites=1), "--prelegal")
        self.assertEqual(stats["cells_without_site"], 1)
        self.assertEqual(stats["plan_cells_left_out"], 1)

    def test_nested_conductors(self):
        nets = [f"- {name} + ROUTED M2 ( {lo} 54 0 ) ( {hi} * 0 ) ;"
                for name, lo, hi in (("long", 90, 630), ("a", 144, 198), ("b", 360, 414))]
        stats = self.run_router(self.frame(nets=nets), "--verify")
        self.assertEqual(stats["verify_short"], 2)

    def test_missing_tracks(self):
        msg = self.run_router(self.frame(tracks=[t for t in TRACKS if " M3 " not in t]), success=False)
        self.assertIn("M3", msg)

    def test_whitespace_and_comments_preserve_routes(self):
        pins = [f"- p{i} + NET sig + DIRECTION INOUT + LAYER M2 ( -20 -9 ) ( 20 9 ) "
                f"+ FIXED ( {x} 54 ) N ;" for i, x in enumerate((90, 306))]
        frame = self.frame(nets=["-\tsig ( PIN p0 ) ( PIN p1 ) ;"], pins=pins)
        frame = frame.replace("NETS 1 ;", "NETS\t1 ; # example: - sig ;")
        stats = self.run_router(frame, "--verify")
        self.assertEqual(stats["nets_routed"], 1)
        out = (self.root / "out.def").read_text()
        self.assertRegex(out, r"\( PIN p1 \)\s+\+ ROUTED M2")
        self.assertIn("# example: - sig ;", out)

    def test_dbu_and_output_errors(self):
        self.assertIn("1000", self.run_router(self.frame().replace("MICRONS 1000", "MICRONS 2000"), success=False))
        self.assertTrue(self.run_router(self.frame(), output="missing/out.def", success=False))

    # ---- verification: every finding carries its net and its geometry ----
    def test_macro_edge_pin_is_reached_through_a_stub(self):
        # The pin is an off-track M3 bar at the bottom edge, under a blockage that covers the macro on M1 to M3.
        self.with_vias()
        block = ("MACRO BLK\n CLASS BLOCK ; ORIGIN 0 0 ; SIZE 0.216 BY 0.150 ;\n PIN P\n  DIRECTION INPUT ; PORT LAYER M3 ; "
                 "RECT 0.100 0.000 0.118 0.054 ; END\n END P\n OBS\n"
                 + "".join(f"  LAYER M{k} ; RECT 0 0 0.216 0.150 ;\n" for k in (1, 2, 3)) + " END\nEND BLK\nEND LIBRARY")
        (self.root / "cells.lef").write_text(CELLS.replace("END LIBRARY", block))
        frame = self.frame(["- big BLK + FIXED ( 306 560 ) N ;", "- a BUF + FIXED ( 108 270 ) N ;"], ["- n ( a Z ) ( big P ) ;"])
        stats = self.run_router(frame, "--verify-report", "found.tsv")
        self.assertEqual((stats["nets_routed"], stats["edge_pin_stubs"], stats["edge_pin_stub_wires"]), (1, 1, 1))
        self.assertEqual({k: v for k, v in stats.items() if k.startswith("verify_") and v}, {})
        self.assertRegex((self.root / "out.def").read_text(), r"M3 \( 415 360 0 \) \( \* 614 0 \)")

    def test_direct_macro_via_keeps_pin_metal_in_the_lef(self):
        self.with_vias()
        block = ("MACRO BLK\n CLASS BLOCK ; ORIGIN 0 0 ; SIZE 0.216 BY 0.216 ;\n"
                 " PIN P\n  DIRECTION INPUT ; PORT LAYER M3 ; RECT 0.100 0.040 0.118 0.112 ; END\n"
                 " END P\nEND BLK\nEND LIBRARY")
        (self.root / "cells.lef").write_text(CELLS.replace("END LIBRARY", block))
        frame = self.frame(["- big BLK + FIXED ( 306 360 ) N ;", "- a BUF + FIXED ( 108 270 ) N ;"],
                           ["- n ( a Z ) ( big P ) ;"])
        stats = self.run_router(frame, "--verify")
        self.assertEqual(stats["nets_routed"], 1)
        self.assertEqual(stats.get("edge_pin_stub_wires", 0), 0)
        self.assertEqual({k: v for k, v in stats.items() if k.startswith("verify_") and v}, {})

    def test_on_track_via_inside_macro_pin_and_one_dbu_outside(self):
        self.with_vias()
        tech = self.root / "tech.lef"
        tech.write_text(tech.read_text().replace(
            "VIA VIA45 DEFAULT\n LAYER M4 ; RECT -0.009 -0.011 0.009 0.011",
            "VIA VIA45 DEFAULT\n LAYER M4 ; RECT -0.023 -0.012 0.023 0.012"))
        block = ("MACRO SRAM\n CLASS BLOCK ; ORIGIN 0 0 ; SIZE 0.216 BY 0.270 ;\n"
                 " PIN P\n DIRECTION INPUT ; PORT LAYER M4 ; RECT 0.036 0.060 0.108 0.084 ; END\n"
                 " END P\nEND SRAM\n")
        (self.root / "cells.lef").write_text(CELLS.replace("END LIBRARY", block + "END LIBRARY"))
        for x, expected in ((301, 0), (302, 1)):
            frame = self.frame(["- ram SRAM + FIXED ( 216 270 ) N ;"],
                               [f"- n ( ram P ) + ROUTED M4 ( {x} 342 ) VIA45 ;"])
            self.run_router(frame, "--verify-report", "found.tsv")
            self.assertEqual(("min-length", "M4", "n") in self.findings(), bool(expected))

    def with_vias(self):
        cuts = "".join(f"LAYER V{k}\n TYPE CUT ;\nEND V{k}\n" for k in range(1, 7))
        vias = ""
        for k in range(1, 7):
            vias += (f"VIA VIA{k}{k + 1} DEFAULT\n LAYER M{k} ; RECT -0.009 -0.011 0.009 0.011 ;\n"
                     f" LAYER V{k} ; RECT -0.009 -0.009 0.009 0.009 ;\n"
                     f" LAYER M{k + 1} ; RECT -0.014 -0.009 0.014 0.009 ;\nEND VIA{k}{k + 1}\n")
        (self.root / "tech.lef").write_text(TECH.replace("END LIBRARY\n", cuts + vias + "END LIBRARY\n"))

    def findings(self):
        rows = [line.split("\t") for line in (self.root / "found.tsv").read_text().splitlines()[1:]]
        return {(r[0], r[1], r[2].split(" (")[0]) for r in rows}

    def two_cells(self, wiring=""):
        cells = ["- a BUF + FIXED ( 108 270 ) N ;", "- b BUF + FIXED ( 324 270 ) N ;"]
        return self.frame(cells, [f"- n ( a Z ) ( b A ) {wiring};"])

    def test_retry_after_deadline(self):
        self.with_vias()
        (self.root / "in.def").write_text(self.two_cells())
        run = subprocess.run([ROUTER, "--agent", "--time-limit", "0.000000001", "--verify",
                              "in.def", "out.def", "tech.lef", "cells.lef"],
                             cwd=self.root, input="retry 300000 1 1 2\nfinish\n",
                             capture_output=True, text=True, check=True, timeout=10)
        initial, resumed, stats = run.stdout.split("\n", 2)
        self.assertEqual(json.loads(initial)["deferred"], 1)
        self.assertEqual(json.loads(resumed)["pending"], 0)
        self.assertEqual(json.loads(stats).get("verify_nets_without_wires", 0), 0)

    def test_reserve_named_cell_access_before_route(self):
        self.with_vias()
        cells = self.root / "access.txt"
        cells.write_text("a\na\n")
        stats = self.run_router(self.two_cells(), "--reserve-cells", str(cells), "--verify")
        self.assertEqual(stats.get("cells_reserved_up_front"), 1)
        self.assertEqual(stats["nets_routed"], 1)
        self.assertEqual({k: v for k, v in stats.items() if k.startswith("verify_") and v}, {})
        cells.write_text("wrong_frame\n")
        self.assertIn("unknown pin-access cell", self.run_router(self.two_cells(), "--reserve-cells", str(cells), success=False))

    def test_live_release_and_retry(self):
        self.with_vias()
        (self.root / "in.def").write_text(self.two_cells())
        run = subprocess.run([ROUTER, "--agent", "--verify", "in.def", "out.def", "tech.lef", "cells.lef"],
                             cwd=self.root, input="release n\nretry 300000 1 1 2\nfinish\n",
                             capture_output=True, text=True, check=True, timeout=10)
        ready, released, retried, final = run.stdout.split("\n", 3)
        self.assertEqual([json.loads(x)["pending"] for x in (ready, released, retried)], [0, 1, 0])
        self.assertEqual(json.loads(final)["verify_open_nets"], 0)

    def test_empty_and_stale_worker_assignment(self):
        self.with_vias()
        assignment = self.root / "nets.txt"
        assignment.write_text("")
        stats = self.run_router(self.two_cells(), "--only", str(assignment), "--verify")
        self.assertEqual(stats["verify_nets_without_wires"], 1)
        assignment.write_text("missing\n")
        self.assertIn("missing", self.run_router(self.two_cells(), "--only", str(assignment), success=False))

    def test_parallel_chunks_and_feedback(self):
        self.with_vias()
        frame = self.two_cells().replace("( 720 720 )", "( 1440 720 )").replace("( 108 270 )", "( 216 270 )")
        (self.root / "in.def").write_text(frame)
        args = [CHUNKS, "--grid", "2", "1", "--out", "trial",
                "in.def", "tech.lef", "cells.lef"]
        trial = subprocess.run(args, cwd=self.root, capture_output=True, text=True, timeout=10)
        self.assertEqual(trial.returncode, 0, trial.stderr)
        self.assertEqual(json.loads(trial.stdout)["retained"], 1)
        self.assertEqual(json.loads(trial.stdout)["violations"], {})
        automatic = subprocess.run([CHUNKS, "--out", "automatic", "in.def", "tech.lef", "cells.lef"],
                                   cwd=self.root, capture_output=True, text=True, check=True, timeout=10)
        self.assertEqual(json.loads(automatic.stdout)["grid"], [2, 1])
        # The agent may release one worker while reusing all of the other packets.
        args[args.index("trial")] = "released"
        reused = subprocess.run([*args, "--reuse", "trial", "--release", "0"], cwd=self.root,
                                capture_output=True, text=True, timeout=10)
        self.assertEqual(reused.returncode, 0, reused.stderr)
        self.assertEqual(json.loads(reused.stdout)["retained"], 0)
        self.assertEqual(json.loads(reused.stdout)["violations"], {})

        self.run_router(frame, "--limit", "0", "--report", "feedback.tsv", output="routed.def")
        repair = subprocess.run([FEEDBACK, "--protect-failures", "feedback.tsv",
                                 "--out", "repair", ".", "tech.lef", "cells.lef"],
                                cwd=self.root, capture_output=True, text=True, timeout=10)
        self.assertEqual(repair.returncode, 0, repair.stderr)
        self.assertEqual(json.loads(repair.stdout)["previous_unrouted"], 1)
        self.assertEqual(json.loads(repair.stdout)["prior_failure_nets"], 1)
        self.assertEqual(json.loads(repair.stdout)["violations"], {})

    def test_targeted_repair_preserves_unselected_open(self):
        self.with_vias()
        frame = self.frame([f"- {name} BUF + FIXED ( {x} 270 ) N ;"
                            for name, x in [("a", 108), ("b", 324), ("c", 216), ("d", 432)]],
                           ["- n ( a Z ) ( b A ) ;", "- later ( c Z ) ( d A ) ;"])
        self.run_router(frame, "--limit", "0", "--report", "feedback.tsv", output="routed.def")
        (self.root / "targets").write_text("n\n")
        repair = subprocess.run([FEEDBACK, "--targets", "targets", "--out", "focus", ".", "tech.lef", "cells.lef"],
                                cwd=self.root, capture_output=True, text=True, check=True, timeout=10)
        result = json.loads(repair.stdout)
        self.assertEqual((result["targeted_unrouted"], result["previous_unrouted"]), (1, 2))
        self.assertEqual(result["violations"], {"verify_nets_without_wires": 1})
        self.assertEqual([r["net"] for r in result["first_failures"]], ["later"])

    def test_placement_home_socket(self):
        self.with_vias()
        (self.root / "in.def").write_text(self.two_cells().replace("( 108 270 )", "( 216 270 )"))
        plan = [{"class": "livehd.placement.chunk_plan", "design": "tiny", "chunks": [
            {"id": "mac", "box_dbu": [0, 0, 500, 720], "region_dbu": [[0, 0, 500, 360], [0, 360, 450, 720]]},
            {"id": "glue", "box_dbu": [500, 0, 720, 720]}]}]
        (self.root / "plan.json").write_text(json.dumps(plan))
        # Bazel's TEST_TMPDIR may exceed the Unix sockaddr path limit.
        with tempfile.TemporaryDirectory(dir="/tmp") as tmp:
            sock = pathlib.Path(tmp) / "route.sock"
            with subprocess.Popen([CHUNKS, "--plan", "plan.json", "--agent-socket", sock, "--out", "homes",
                                   "in.def", "tech.lef", "cells.lef"], cwd=self.root,
                                  stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True) as proc:
                try:
                    deadline = time.monotonic() + 5
                    while not sock.exists() and proc.poll() is None and time.monotonic() < deadline:
                        time.sleep(0.02)
                    self.assertTrue(sock.exists())
                    for cmd in ("status", "finish"):
                        conn = http.client.HTTPConnection("localhost", timeout=5)
                        with socket.socket(socket.AF_UNIX) as link:
                            link.connect(str(sock))
                            conn.sock = link
                            conn.request("POST", "/agent", body=json.dumps({"cmd": cmd}))
                            reply = json.loads(conn.getresponse().read())
                            self.assertTrue(reply["ok"], reply)
                    stdout, stderr = proc.communicate(timeout=5)
                    self.assertEqual(proc.returncode, 0, stderr)
                    self.assertEqual(json.loads(stdout)["homes"], ["mac", "glue"])
                    self.assertFalse(sock.exists())
                finally:
                    if proc.poll() is None:
                        proc.terminate()

    def test_long_share_and_invalid_fractions(self):
        self.with_vias()
        stats = self.run_router(self.two_cells(), "--long-frac", "1")
        self.assertEqual(stats["long_first_nets"], 1)
        for option, value in [("--space-frac", "1.2"), ("--long-frac", "nan"), ("--pd-alpha", "-1")]:
            self.assertIn("between", self.run_router(self.two_cells(), option, value, success=False))

    def test_complete_net_verifies_clean(self):
        self.with_vias()
        stats = self.run_router(self.two_cells(), "--verify-report", "found.tsv")
        self.assertEqual(stats["nets_routed"], 1)
        self.assertEqual({k: v for k, v in stats.items() if k.startswith("verify_") and v}, {})
        self.assertEqual(self.findings(), set())

    def test_omitted_net_is_reported(self):
        self.with_vias()
        stats = self.run_router(self.two_cells(), "--limit", "0", "--verify-report", "found.tsv")
        self.assertEqual(stats["verify_nets_without_wires"], 1)
        self.assertEqual(self.findings(), {("no-wires", "", "n")})

    def test_dangling_via_off_its_pin(self):
        self.with_vias()
        wiring = "+ ROUTED M1 ( 600 414 ) VIA12 NEW M2 ( 586 414 0 ) ( 650 * 0 ) "
        stats = self.run_router(self.two_cells(wiring), "--verify-report", "found.tsv")
        self.assertEqual(stats["verify_v1_off_pin"], 1)
        self.assertEqual(stats["verify_open_nets"], 1)
        self.assertEqual(self.findings(), {("v1-off-pin", "M1", "n"), ("open", "", "n")})
        self.assertIn("\t591\t403\t609\t425", (self.root / "found.tsv").read_text())

    def test_foreign_nets_on_a_terminal(self):
        pins = ["- p0 + NET sig + DIRECTION INOUT + LAYER M2 ( -20 -9 ) ( 20 9 ) + FIXED ( 306 54 ) N ;"]
        nets = ["- sig ( PIN p0 ) ;", "- b + ROUTED M2 ( 250 54 0 ) ( 300 * 0 ) ;", "- c + ROUTED M2 ( 320 54 0 ) ( 400 * 0 ) ;"]
        stats = self.run_router(self.frame(nets=nets, pins=pins), "--verify-report", "found.tsv")
        self.assertEqual(stats["verify_short"], 2)
        # The two foreign wires are also closer than the end-to-end spacing across the 40 nm port.
        self.assertEqual(self.findings(), {("short", "M2", "b sig"), ("short", "M2", "sig c"), ("end-to-end", "M2", "b c")})


if __name__ == "__main__":
    unittest.main()
