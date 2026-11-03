# Root package and regression-selection smoke check.

load("//tools:regression.bzl", "sh_test")

sh_test(
    name = "regression_selection_smoke",
    srcs = ["scripts/regression_smoke.py"],
    data = ["scripts/regression.py"],
)

exports_files(["MODULE.bazel"])

# Keep the normal build unchanged; the native USYN CLI also builds without
# optional ABC-backed tools, even when the ABC package/repository is absent.
config_setting(
    name = "without_abc",
    define_values = {"livehd_abc": "false"},
    visibility = ["//visibility:public"],
)
