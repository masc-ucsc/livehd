#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
cc_library(
   name = "slang",
   srcs = glob(["source/**/*.cpp","external/xxhash/*.c"]),
   hdrs = glob(["source/**/*.h","include/slang/**/*.h", "external/*.hpp", "external/gsl/*", "external/xxhash/*.h"]),
   includes = [ "include", "external" ], # Needed because some includes use <foo.h>
   visibility = ["//visibility:public"],
   deps = ["@fmt",
           "@json",
   ],
)
