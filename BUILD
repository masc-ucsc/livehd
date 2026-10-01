# Empty BUILD file, just to make this directory a Bazel package.

exports_files(["MODULE.bazel"])

# Keep the normal build unchanged; the native USYN CLI also builds without
# optional ABC-backed tools, even when the ABC package/repository is absent.
config_setting(
    name = "without_abc",
    define_values = {"livehd_abc": "false"},
    visibility = ["//visibility:public"],
)
