#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_binary(
    name = "import_library",
    srcs = ["import_library.cpp"] + glob(["*.hpp"]),
    linkopts = ['-lpthread'],
    deps = [
        "//meta:meta",
    ],
)
