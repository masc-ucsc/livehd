project(pass_sample)

FILE(GLOB pass_sample_SOURCE *.cpp)
FILE(GLOB pass_sample_HEADER *.hpp)

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

ADD_LIBRARY(pass_sample ${pass_sample_SOURCE} ${pass_sample_HEADER})

