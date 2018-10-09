#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "riscv_verify",
    srcs = glob(["*.cpp"])
         + glob(["riscvdis/*.c"])
         + glob(["rscvemu/*.c"],
                exclude=[ "aes.c",
                         "build_filelist.c",
                         "jsemu.c",
                         "verification_harness.c",
                         "x86_machine.c",
                         ])
    ,
    hdrs = glob(["*.h"])
         + glob(["riscvdis/*.h"])
         + glob(["riscvemu/*.h"])
    ,
    visibility = ["//visibility:public"],
    includes = [".","riscvemu"],
)

