project(pass_gvn_pre)

FILE(GLOB pass_gvn_pre_SOURCE *.cpp)
FILE(GLOB pass_gvn_pre_HEADER *.hpp)

#SET(EXELIST "lgrand")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

ADD_LIBRARY(pass_gvn_pre ${pass_gvn_pre_SOURCE} ${pass_gvn_pre_HEADER})

