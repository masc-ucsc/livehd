project(inou_rand)

FILE(GLOB inou_rand_SOURCE *.cpp)
FILE(GLOB inou_rand_HEADER *.hpp)

SET(EXELIST "")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

ADD_LIBRARY(inou_rand ${inou_rand_SOURCE} ${inou_rand_HEADER})

