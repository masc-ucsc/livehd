load("@rules_shell//shell:sh_binary.bzl", "sh_binary")
load("@rules_shell//shell:sh_test.bzl", "sh_test")

filegroup(
    name = "fixtures",
    srcs = glob(["*.prp", "*.def"]) + ["benchmarks.json", "hierarchy_hints.json", "fake.lef", "fake.lib", "macros.v", "timing.sdc"],
)

sh_binary(
    name = "run",
    srcs = ["run.py"],
    data = [":fixtures", "generate.py", "render.py", "//lhd:lhd"],
)

# Exercise every physical stage and handoff on the full fixtures. These checks
# require legal placement, not optimization convergence; keep the optimization
# budget bounded. The run binary retains the longer measurement settings.
[
    sh_test(
        name = case,
        size = "small",
        srcs = ["run.py"],
        args = ["--case", case, "--iterations", "20"],
        data = [":fixtures", "generate.py", "render.py", "//lhd:lhd"],
        tags = ["placement"],
    )
    for case in ["mesh_2x2", "mesh_4x4", "ring_32", "hierarchy_4x4", "mesh_6x6"]
]

sh_test(
    name = "regularity_checks",
    size = "small",
    srcs = ["regularity_checks.py"],
    data = [":fixtures", "generate.py", "render.py"],
)

filegroup(
    name = "hint_fixtures",
    srcs = ["fake.lef", "fake.lib", "macros.v"],
    visibility = ["//pass/place:__pkg__"],
)
