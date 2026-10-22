load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

filegroup(
    name = "fixtures",
    srcs = glob(["*.prp", "*.def"]) + ["benchmarks.json", "fake.lef", "fake.lib", "macros.v", "timing.sdc"],
)

sh_binary(
    name = "run",
    srcs = ["run.py"],
    data = [":fixtures", "generate.py", "render.py", "//lhd:lhd"],
)

# Default suite excludes long1. Opt in with --test_tag_filters=placement
# and --test_timeout=3600. No external simulator, PDK, or Python package needed.
[
    sh_test(
        name = case,
        size = "large",
        timeout = "eternal",
        srcs = ["run.py"],
        args = ["--case", case],
        data = [":fixtures", "generate.py", "render.py", "//lhd:lhd"],
        tags = ["long1", "placement"],
    )
    for case in ["mesh_2x2", "mesh_4x4", "ring_32", "hierarchy_4x4", "mesh_6x6"]
]

sh_test(
    name = "regularity_checks",
    size = "small",
    srcs = ["regularity_checks.py"],
    data = [":fixtures", "generate.py", "render.py"],
)
