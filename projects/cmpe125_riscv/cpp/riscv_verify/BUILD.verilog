#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
    name = "riscv_verify",
    srcs = glob(["*.cpp"])
         + glob(["riscvdis/*.cc"])
         + glob(["riscvemu/*.c"],
                exclude=[ "riscvemu/aes.c",
                         "riscvemu/build_filelist.c",
                         "riscvemu/jsemu.c",
                         "riscvemu/verification_harness.c",
                         "riscvemu/x86_machine.c",
                         ])
    ,
    hdrs = glob(["*.h"])
         + glob(["riscvdis/*.h"])
         + glob(["riscvemu/*.h"])
    ,
    visibility = ["//visibility:public"],
    includes = [".","riscvemu"],
    defines = ["VERIFICATION"],
)

