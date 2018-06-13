cc_library(
    name = "core",
    #toolchains = ['//tools:clang-local'],
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    linkopts = ['-lpython3.6m', '-lpthread'],
    deps = ["@sparsehash//:headers",
            "@bm//:headers",
            "@pybind11//:headers",
            "@python3//:headers",
            "@spdlog//:headers",
            "@boost//:filesystem", "@boost//:program_options",
            ],
)

