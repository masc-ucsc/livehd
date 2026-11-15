# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
import tempfile
import unittest
from pathlib import Path

from tools.placereason_score import final_drc, final_setup_timing, routed_endpoints


class RoutedTimingTest(unittest.TestCase):
    # Earlier reports are real but refer to a different graph; OpenSTA also
    # prints zero WNS/TNS when the final graph has no constrained paths.
    pre_route = "wns max -12.00\ntns max -90.20\nROUTE_ALL_DONE\n"

    def test_only_a_final_report_establishes_drc(self):
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory) / "final.drc.rpt"
            self.assertIsNone(final_drc(report))
            for contents, expected in [("", 0), ("router aborted", None),
                                       ("violation type: Short\nviolation type: Metal Spacing\n", 2)]:
                report.write_text(contents)
                self.assertEqual(final_drc(report), expected)

    def test_only_measured_final_path_supplies_numbers(self):
        final = "LIVEHD_SETUP_BEGIN\nwns max 0.00\ntns max 0.00\nLIVEHD_SETUP_END\n"
        result = final_setup_timing(self.pre_route + final)
        self.assertEqual((result["status"], result["wns"], result["tns"]), ("measured", 0.0, 0.0))
        for final in ("wns max 0.00\ntns max 0.00\nLIVEHD_SETUP_NO_PATH\n",
                      "LIVEHD_SETUP_BEGIN\nwns max -0.16\ntns max -0.91\n",
                      "LIVEHD_SETUP_BEGIN\nwns max INF\ntns max 0.00\nLIVEHD_SETUP_END\n"):
            with self.subTest(final=final):
                result = final_setup_timing(self.pre_route + final)
                self.assertEqual(result["status"], "unavailable")
                self.assertNotIn("wns", result)
                self.assertNotIn("tns", result)

    def test_endpoint_census_keeps_each_clock_but_counts_a_physical_pin_only_once(self):
        # Q fails on two capture clocks; P passes. The global ledger attributes Q
        # to its worst path family. Removing clock B must invalidate the comparison
        # even though the global endpoint count and both STA totals stay identical.
        with tempfile.TemporaryDirectory() as directory:
            report = Path(directory)/"pins.tsv"
            header = "endpoint_hex\tclock_hex\tlaunch_hex\tfamily\tslack_ns\n"
            a = "51\t41\t61\tregister_to_macro\t-2\n"
            b = "51\t42\t62\tregister_to_register\t-1\n"
            p = "50\t41\t63\tio\t3\n"
            log = "LIVEHD_CLOCK 41 10\nLIVEHD_CLOCK 42 20\nLIVEHD_ENDPOINT_TOTAL -2 -2 3\n"
            report.write_text(header+a+b+p)
            both = routed_endpoints(report, log)
            self.assertEqual((both["total"]["endpoints"], both["total"]["failing"], both["total"]["tns_ps"]), (2, 1, -2000))
            self.assertEqual(both["capture_clocks"]["B"]["tns_ps"], -1000)
            self.assertEqual(set(both["path_families"]), {"io", "register_to_macro"})
            self.assertEqual(both["path_families"]["io"]["worst_slack_ps"], 3000)
            report.write_text(header+p+a)
            one = routed_endpoints(report, log)
            self.assertEqual(both["total"]["population"], one["total"]["population"])
            self.assertNotEqual(both["total"]["clock_population"], one["total"]["clock_population"])
            report.write_text(header+b+p)
            self.assertEqual(routed_endpoints(report, log)["status"], "unavailable")


if __name__ == "__main__":
    unittest.main()
