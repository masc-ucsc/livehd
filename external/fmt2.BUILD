cc_library(
    name = "fmt",
    hdrs = glob(["include/**/*.h"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    defines = ["FMT_HEADER_ONLY"], # Important: use header-only mode
    data = glob(["**"]),
)

filegroup(
    name = "all",
    srcs = glob(["**"]),
    visibility = ["//visibility:public"],
)
