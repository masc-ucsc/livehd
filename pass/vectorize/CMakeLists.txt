project(pass_vectorize)

FILE(GLOB pass_vectorize_SOURCE *.cpp)
FILE(GLOB pass_vectorize_HEADER *.hpp)

#SET(EXELIST "lgrand")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

ADD_LIBRARY(pass_vectorize ${pass_vectorize_SOURCE} ${pass_vectorize_HEADER})

