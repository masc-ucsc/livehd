project(pass_cse)

FILE(GLOB pass_cse_SOURCE *.cpp)
FILE(GLOB pass_cse_HEADER *.hpp)

#SET(EXELIST "lgrand")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

ADD_LIBRARY(pass_cse ${pass_cse_SOURCE} ${pass_cse_HEADER})

