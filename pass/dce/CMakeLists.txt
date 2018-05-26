project(pass_dce)

FILE(GLOB pass_dce_SOURCE *.cpp)
FILE(GLOB pass_dce_HEADER *.hpp)

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

ADD_LIBRARY(pass_dce ${pass_dce_SOURCE} ${pass_dce_HEADER})

