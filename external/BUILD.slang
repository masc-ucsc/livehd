#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
#  slang generates cpp at build time. The patch has the build/source directory
#  cd slang_orig/build/
#  cmake ../
#  make
#  cp -a source/* ../../slang/generated
#  rm the llvm source/codegen/
#  cd ../../slang
#  rm -rf ../slang_orig/build
#  mkpatch ../slang_orig .
cc_library(
   name = "slang",
   srcs = glob(["source/**/*.cpp",
                "generated/*.cpp",
                "external/xxhash/*.cpp",
                "generated/**/*.cpp"],
                exclude=[
                 "source/codegen/CGExpr.cpp",
                 "source/codegen/CGSysCall.cpp",
                 "source/codegen/CodeGenerator.cpp",
                 "source/codegen/CodeGenFunction.cpp",
                 "source/codegen/CodeGenTypes.cpp",
                 "source/codegen/JIT.cpp",
               ]),
   hdrs = glob(["source/**/*.h",
                "include/slang/**/*.h",
                "generated/slang/**/*.h",
                "generated/source/slang/**/*.h",
                "external/*.hpp",
                "external/gsl/*",
                "external/fmt/*.h",
                "external/fmt/*.cc",
                "external/xxhash/*.h",
                "external/ieee1800/*.h",
              ]),
   includes = [ "include", "external", "generated" ,"generated/source"], # Needed because some includes use <foo.h>
   visibility = ["//visibility:public"],
   copts = ["-Wno-error", "-O2"],
   deps = ["@fmt",
           "@json",
   ],
)
