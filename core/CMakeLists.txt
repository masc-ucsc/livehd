project(core)

FILE(GLOB core_SOURCE *.cpp)
FILE(GLOB core_HEADER *.hpp)

ADD_LIBRARY(core ${core_SOURCE} ${core_HEADER})

ADD_SUBDIRECTORY(tests)
