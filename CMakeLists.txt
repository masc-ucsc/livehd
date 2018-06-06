SET(CMAKE_C_COMPILER   "/usr/bin/clang")
SET(CMAKE_CXX_COMPILER "/usr/bin/clang++")

cmake_minimum_required(VERSION 3.9)

project(lgraph)

enable_testing()

# C++1z is enough, but C++14 is supposed to be more standard/stable
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

FIND_PACKAGE(Threads REQUIRED)
FIND_PACKAGE(Boost REQUIRED)
FIND_PACKAGE(BISON REQUIRED)
FIND_PACKAGE(TCL REQUIRED)
FIND_PACKAGE(RapidJSON REQUIRED)

SET(BOOST_LIBS "-lboost_filesystem -lboost_program_options -lboost_system -lboost_iostreams -lboost_serialization")
SET(REQUIRED_LIBS "")

include(ExternalProject)

IF(CMAKE_BUILD_TYPE_INITIALIZED_TO_DEFAULT)
  SET(CMAKE_BUILD_TYPE Release)
ENDIF(CMAKE_BUILD_TYPE_INITIALIZED_TO_DEFAULT)

IF(CMAKE_BUILD_TYPE MATCHES "Debug")
  MESSAGE("  -DCMAKE_BUILD_TYPE=Debug      debug compilation options")
  SET(CMAKE_CXX_FLAGS       "-DLIN64 -fPIC -g3 -Wall -Wno-overloaded-virtual -O0 -DDEBUG=1 -D_YOSYS_")
  SET(CMAKE_C_FLAGS         "-DLIN64 -fPIC -g3 -Wall -O0 -DDEBUG=1 -D_YOSYS_")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
ELSE(CMAKE_BUILD_TYPE MATCHES "Release")
  MESSAGE("  -DCMAKE_BUILD_TYPE=Release    release compilation options (default)")
  SET(CMAKE_CXX_FLAGS       "-DLIN64 -fPIC -Ofast -mtune=native -DNDEBUG -D_YOSYS_")
  SET(CMAKE_C_FLAGS         "-DLIN64 -fPIC -Ofast -mtune=native -DNDEBUG -D_YOSYS_")
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
ENDIF(CMAKE_BUILD_TYPE MATCHES "Debug")


##########################################################
# MISC
INCLUDE_DIRECTORIES(misc/lef/lef)
INCLUDE_DIRECTORIES(misc/def/def)
INCLUDE_DIRECTORIES(misc/ezsat)

ADD_SUBDIRECTORY(misc/lef/lef)
ADD_SUBDIRECTORY(misc/def/def)
ADD_SUBDIRECTORY(misc/ezsat)

##########################################################
# SUBS
ExternalProject_Add(yosys
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/subs/yosys
  GIT_SUBMODULES yosys
  CONFIGURE_COMMAND make config-clang
  PREFIX ""
  BUILD_COMMAND make $(MFLAGS)
  BUILD_IN_SOURCE 1
  TMP_DIR ${CMAKE_CURRENT_BINARY_DIR}/subs/yosys/tmp
  STAMP_DIR ${CMAKE_CURRENT_BINARY_DIR}/subs/yosys/stampdir
  DOWNLOAD_COMMAND ""
  INSTALL_COMMAND make $(MFLAGS) -C ${PROJECT_SOURCE_DIR}/subs/yosys install PREFIX=${CMAKE_BINARY_DIR}/subs/yosys
  )

include_directories ("${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/subs")

# Include-only libraries
INCLUDE_DIRECTORIES(subs/sparsehash-c11)
INCLUDE_DIRECTORIES(subs/BitMagic/src)
INCLUDE_DIRECTORIES(subs/spdlog/include)
INCLUDE_DIRECTORIES(subs/yosys)

# code and include libraries
ADD_SUBDIRECTORY(core)
ADD_SUBDIRECTORY(pass/sample)
ADD_SUBDIRECTORY(pass/dce)
ADD_SUBDIRECTORY(pass/cse)
ADD_SUBDIRECTORY(pass/gvn_pre)
ADD_SUBDIRECTORY(pass/fluid)
ADD_SUBDIRECTORY(pass/vectorize)
ADD_SUBDIRECTORY(pass/lgopt)
ADD_SUBDIRECTORY(pass/lgopt_dce)
ADD_SUBDIRECTORY(pass/lgopt_gvn_pre)
ADD_SUBDIRECTORY(pass/lgopt_fluid)
ADD_SUBDIRECTORY(pass/droute)
ADD_SUBDIRECTORY(pass/dfg)
ADD_SUBDIRECTORY(inou/lefdef)
ADD_SUBDIRECTORY(inou/rand)
ADD_SUBDIRECTORY(inou/yosys)
ADD_SUBDIRECTORY(inou/dump)
ADD_SUBDIRECTORY(inou/pyrope)
ADD_SUBDIRECTORY(inou/tech)
ADD_SUBDIRECTORY(cops/live)

ADD_SUBDIRECTORY(inou/json)
ADD_SUBDIRECTORY(inou/abc)
ADD_SUBDIRECTORY(inou/lef)
ADD_SUBDIRECTORY(inou/sat)
ADD_SUBDIRECTORY(inou/cfg)

add_test(NAME basic_inou_rand_json        COMMAND  ${PROJECT_SOURCE_DIR}/test/rand_json.sh)
SET_TESTS_PROPERTIES(basic_inou_rand_json PROPERTIES DEPENDS core_done)


# add your test bellow, please add either basic, compl or extra in the test name
# your test script should return 0 if it succeeds or anything else if it fails
# alternatively, you can add tests to the CMakeLists.txt file in your subdirectory

add_test(NAME basic_inou_yosys         COMMAND  ${PROJECT_SOURCE_DIR}/test/yosys.sh)
add_test(NAME basic_inou_synth_yosys   COMMAND  ${PROJECT_SOURCE_DIR}/test/synth.sh -s${PROJECT_SOURCE_DIR})
add_test(NAME basic_inou_abc           COMMAND  ${PROJECT_SOURCE_DIR}/test/abc.sh)
add_test(NAME basic_inou_lefdef        COMMAND  ${PROJECT_SOURCE_DIR}/test/lefdef.sh -s${PROJECT_SOURCE_DIR})
add_test(NAME basic_lgopt_dce          COMMAND  ${PROJECT_SOURCE_DIR}/test/lgopt_dce.sh)
add_test(NAME basic_lgopt_gvn_pre      COMMAND  ${PROJECT_SOURCE_DIR}/test/lgopt_gvn_pre.sh)

add_test(NAME compl_tmap_verilog       COMMAND  ruby ${PROJECT_SOURCE_DIR}/inou/tech/verilog.rb ${PROJECT_SOURCE_DIR}//subs/yosys/techlibs/common/simcells.v > tech_library)
add_test(NAME compl_lgopt_fluid        COMMAND  ${PROJECT_SOURCE_DIR}/test/lgopt_fluid.sh)

add_test(NAME extra_inou_ANUBIS-alpha  COMMAND  ${PROJECT_SOURCE_DIR}/inou/yosys/lgyosys --lgdb=al_lgdb --testdir=${PROJECT_SOURCE_DIR}/subs/anubis/alpha CONFIGURATIONS complete)
add_test(NAME extra_inou_ANUBIS-dlx    COMMAND  ${PROJECT_SOURCE_DIR}/inou/yosys/lgyosys --lgdb=dl_lgdb --testdir=${PROJECT_SOURCE_DIR}/subs/anubis/dlx CONFIGURATIONS complete)
add_test(NAME extra_inou_ANUBIS-fpu    COMMAND  ${PROJECT_SOURCE_DIR}/inou/yosys/lgyosys --lgdb=fp_lgdb --testdir=${PROJECT_SOURCE_DIR}/subs/anubis/fpu/rtl/ CONFIGURATIONS complete)

# only run in the complete regression
add_test(NAME extra_inou_ANUBIS-mor1kx COMMAND  ${PROJECT_SOURCE_DIR}/inou/yosys/lgyosys --lgdb=mo_lgdb --testdir=${PROJECT_SOURCE_DIR}/subs/anubis/mor1kx/rtl/verilog CONFIGURATIONS complete)
add_test(NAME extra_inou_ANUBIS-or1200 COMMAND  ${PROJECT_SOURCE_DIR}/inou/yosys/lgyosys --lgdb=or_lgdb --testdir=${PROJECT_SOURCE_DIR}/subs/anubis/or1200 CONFIGURATIONS complete)
add_test(NAME extra_inou_BOOM          COMMAND  ${PROJECT_SOURCE_DIR}/inou/yosys/lgyosys --lgdb=bo_lgdb ${PROJECT_SOURCE_DIR}/test/benchmarks/boom/boombase.v ${PROJECT_SOURCE_DIR}/test/benchmarks/boom/boom.fix.v --top=BoomCore CONFIGURATIONS complete)

