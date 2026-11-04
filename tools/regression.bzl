"""Test wrappers that retain both smoke sets in each regression selection."""

load("@rules_cc//cc:defs.bzl", _cc_test = "cc_test")
load("@rules_python//python:defs.bzl", _py_test = "py_test")
load("@rules_shell//shell:sh_test.bzl", _sh_test = "sh_test")

_BACKEND = [
    "inou/lefdef", "pass/place", "pass/domplace", "pass/opentimer",
    "pass/liberty", "tools/uroute", "tools/spef", "scripts/domplace",
]
_BOTH = ["lhd", "pass/abc", "pass/usyn", "pass/synth", "pass/domino", "tools", "benchmark"]
_SMOKE = [
    "//:regression_selection_smoke",
    "//graph:cell_test",
    "//lhd:lhd_options_test",
    "//pass/place:place_test",
    "//pass/place:legroute_test",
    "//inou/lefdef:signal_via_test",
]

def _under(path, prefix):
    return path == prefix or path.startswith(prefix + "/")

def _tags(name, tags):
    package = native.package_name()
    label = "//" + package + ":" + name
    if any([_under(package, prefix) for prefix in _BACKEND]):
        domains = ["backend"]
    elif not package or any([_under(package, prefix) for prefix in _BOTH]):
        domains = ["frontend", "backend"]
    else:
        domains = ["frontend"]
    # A stable census sample supplements the named integration checks. Give
    # smoke tests both domain tags so either config always retains both sets.
    if label in _SMOKE or hash(label) % 10 == 0 or "regression-smoke" in tags:
        domains = ["frontend", "backend", "regression-smoke"]
    return sorted({tag: True for tag in tags + domains}.keys())

def cc_test(name, tags = [], **kwargs):
    """Declare a C++ test with regression-domain tags."""
    _cc_test(name = name, tags = _tags(name, tags), **kwargs)

def py_test(name, tags = [], **kwargs):
    """Declare a Python test with regression-domain tags."""
    _py_test(name = name, tags = _tags(name, tags), **kwargs)

def sh_test(name, tags = [], **kwargs):
    """Declare a shell test with regression-domain tags."""
    _sh_test(name = name, tags = _tags(name, tags), **kwargs)
