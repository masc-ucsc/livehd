cc_library(
    name = "core",
    #toolchains = ['//tools:clang-local'],
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    deps = ["@sparsehash//:headers",
            "@bm//:headers",
            "@spdlog//:headers",
            ],
)

