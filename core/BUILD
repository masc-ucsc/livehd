cc_library(
    name = "core",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    visibility = ["//visibility:public"],
    includes = ["."],
    linkopts = ['-lpthread'],
    deps = ["@sparsehash//:headers",
            "@bm//:headers",
            "@spdlog//:headers",
            ],
)
