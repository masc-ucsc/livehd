#  This file is distributed under the BSD 3-Clause License. See LICENSE for details.
#
#  slang generates cpp at build time. The patch has the build/source directory
#BUILD slang to create the generated files
#  clone slang in slang and slang_orig
#  cd slang/build/
#  cmake ../
#  make
#  cd ../
#MOVE generated to main directory && remove build && unnecessary files for patch
#  mv build/source generated
#  rm -rf build
#  rm -rf generated/CMake*
#  rm -rf generated/Make*
#  rm -rf generated/cmake*
#CREATE the patch file
#  mkpatch ../slang_orig .
#
#  Update the inou/slang/slang_driver.cpp
#
#  Mostly:
#  -add includes
#  -add Slang_tree::proce... call
#  -s/OS::printE/Pass::error/g
#  -Look for s/int main/int slang_main/g
#
cc_library(
   name = "slang",
   srcs = glob(["source/**/*.cpp",
                "generated/*.cpp",
                "external/xxhash/*.cpp",
                "generated/**/*.cpp"
               ],
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
               ],
                exclude=[
                "source/codegen/CGBuilder.h",
                "source/codegen/CodeGenFunction.h",
                "source/codegen/CodeGenTypes.h",
               ]),
   includes = [ "include", "external", "generated" ,"generated/source"], # Needed because some includes use <foo.h>
   visibility = ["//visibility:public"],
   copts = ["-w", "-O2"],
   deps = ["@fmt",
           "@json",
   ],
)
