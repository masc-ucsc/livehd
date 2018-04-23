project(pass_fluid)

FILE(GLOB pass_fluid_SOURCE *.cpp)
FILE(GLOB pass_fluid_HEADER *.hpp)

#SET(EXELIST "lgrand")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

ADD_LIBRARY(pass_fluid ${pass_fluid_SOURCE} ${pass_fluid_HEADER})

