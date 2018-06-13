cc_library(
    name = "inou_yosys",
    srcs = glob(["*.cpp"]),
    hdrs = glob(["*.hpp"]),
    #alwayslink=1,
    defines = ["_YOSYS_","YOSYS_ENABLE_PLUGINS"],
    visibility = ["//visibility:public"],
    deps = [
        "@yosys//:yosysincludes",
        "//core:core",
    ],
)

#genrule(
#    name = "concat",
#    output_to_bindir = 1,
#    srcs = [
#        "dump_yosys.cpp",
#        ":inou_yosys",
#        "//core:core",
#        "@boost//:filesystem", "@boost//:program_options",
#        "@boost//:system",
#    ],
#    outs = ["concatenated.txt"],
#    cmd = "echo $(CC) $(CC_FLAGS) -fPIC -shared $(BINDIR)/inou/yosys/_objs/inou_yosys/inou/yosys/*.o $(locations //core:core) -Wl,-Bstatic $(locations @boost//:filesystem) $(locations @boost//:program_options) $(locations @boost//:system) -Wl,-Bdynamic -lrt -lpthread > $@",
#)

load("//tools:linkso.bzl", "linkso")

linkso(
    name = "liblgraph_yosys",
    srcs   = [":inou_yosys", "//core:core"],
    static = ["@boost//:system" , "@boost//:filesystem", "@boost//:program_options"],
)

