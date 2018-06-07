
cc_binary(
    name = "import_library",
    srcs = ["import_library.cpp"] + glob(["*.hpp"]),
    linkopts = ['-lpthread'],
    deps = [
        "//core:core",
    ],
)
