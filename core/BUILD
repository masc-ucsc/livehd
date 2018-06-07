cc_library(
    name = "core",
    #toolchains = ['//tools:clang-local'],
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    deps = ["@sparsehash//:headers",
            "@bm//:headers",
            "@spdlog//:headers",
            "@boost//:filesystem", "@boost//:program_options",
            ],
)

