cc_library(
    name = "mockturtle",
    hdrs = glob(["include/**/*.hpp"]),
    includes = ["include"],
    visibility = ["//visibility:public"],
    deps = [
        "@kitty//:kitty",
        "@parallel_hashmap//:parallel_hashmap",
        "@fmt2//:fmt",
    ],
    defines = [
        "DISABLE_ABC",  # Disable ABC dependency
        "DISABLE_BILL", # Disable bill dependency
    ],
)
