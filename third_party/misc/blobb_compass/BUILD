#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.

cc_library(
    name = "blobb",
    srcs = glob(["BloBB/*.cpp", "BTree/*.cpp", "common/*.cpp", "CompaSS/*.cpp"], exclude = ["BTree/*tst.cpp", "CompaSS/main.cpp"]),
    hdrs = glob(["BloBB/*.hpp", "BTree/*.hpp", "common/*.hpp", "CompaSS/*.hpp"], exclude = ["BTree/*tst.hpp", "CompaSS/main.hpp"]),
    copts = ["-O3", "-std=c++98", "-IBloBB", "-IBTree", "-Icommon", "-ICompaSS"],
    visibility = ["//visibility:public"],
    includes = ["BloBB", "BTree", "common", "CompaSS", "."],
)
