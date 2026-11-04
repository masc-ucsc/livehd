#!/usr/bin/env python3
"""A successful later mode cannot hide an earlier fixture failure."""
from tempfile import TemporaryDirectory
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

from prplib import PrpRunner, golden_hdl


class HdlPairs(unittest.TestCase):
    def test_one_hdl_source_per_pair(self):
        with TemporaryDirectory() as directory:
            prp = Path(directory) / 'sample.prp'
            prp.with_suffix('.sv').touch()
            self.assertEqual(golden_hdl(str(prp)), str(prp.with_suffix('.sv')))
            prp.with_suffix('.v').touch()
            with self.assertRaisesRegex(ValueError, 'ambiguous HDL pair'):
                golden_hdl(str(prp))

    def test_systemverilog_uses_the_existing_direct_slang_proof(self):
        runner = PrpRunner.__new__(PrpRunner)
        with patch('prplib.subprocess.run') as compile_hdl:
            self.assertEqual(runner.run_hdl_reader_pair(
                '.', None, 'source.sv', 'top', 'top', '.', 'proven'), 0)
            compile_hdl.assert_not_called()



class ModeFailures(unittest.TestCase):
    def test_every_requested_mode_must_pass(self):
        for statuses in [(1, 0), (0, 1), (0, 0)]:
            with self.subTest(statuses=statuses):
                runner = PrpRunner.__new__(PrpRunner)
                runner.prebuild_ln_files = Mock(return_value=0)
                runner._run_compile = Mock(side_effect=statuses)
                runner.check_expect_instances = Mock(return_value=0)
                fixture = SimpleNamespace(params={"type": ["parsing", "lnast"]})
                result = runner.run(".", fixture)
                self.assertEqual(bool(result), any(statuses))
                self.assertEqual(
                    [call.args[2] for call in runner._run_compile.call_args_list],
                    ["parsing", "lnast"],
                )


class OracleFailures(unittest.TestCase):
    def test_setup_errors_and_crashes_fail_but_inconclusive_is_distinct(self):
        for status in [0, 1, 2, 5, 124, -11]:
            with self.subTest(status=status), TemporaryDirectory() as directory:
                runner = PrpRunner.__new__(PrpRunner)
                runner._lgcheck_tools = Mock(return_value=("/staged/lgcheck", "/staged/yosys"))
                process = Mock(returncode=status)
                process.communicate.return_value = (b"oracle verdict\n", None)
                fixture = SimpleNamespace(params={"name": "oracle"})
                with patch("prplib.subprocess.Popen", return_value=process):
                    result = runner.run_yosys_lec(
                        fixture, "impl.v", "top", "gold.v", "top", directory
                    )
                self.assertEqual(result == 0, status in [0, 2, 124])

    def test_missing_independent_oracle_fails(self):
        runner = PrpRunner.__new__(PrpRunner)
        runner._lgcheck_tools = Mock(return_value=(None, None))
        fixture = SimpleNamespace(params={"name": "oracle"})
        self.assertNotEqual(runner.run_yosys_lec(fixture, "i.v", "top", "r.v", "top", "."), 0)


if __name__ == "__main__":
    unittest.main()
