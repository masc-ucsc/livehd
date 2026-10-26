# This file is distributed under the BSD 3-Clause License. See LICENSE for details.
# 2d-domplace H: the matched three-arm runner and the run views it shares
# with the domplace smokes (todo/livehd/2d-domplace.html).
load("@rules_shell//shell:sh_test.bzl", "sh_test")

exports_files(
    ["domplace_view.py"],
    visibility = ["//visibility:public"],
)

# The runner's dry-run plan on the in-repo sky130 fixture, a real run of
# arms 1 and 3 on both core modes, and a warm rerun.
sh_test(
    name = "arms_smoke",
    size = "small",
    timeout = "short",
    srcs = ["arms_smoke.py"],
    data = [
        "arms.py",
        "domplace_view.py",
        "//inou/lefdef:test_data",
        "//lhd",
    ],
)
