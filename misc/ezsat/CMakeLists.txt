project(ezsat)

FILE(GLOB ezsat_SOURCE *.cpp)
FILE(GLOB ezsat_HEADER *.hpp)

SOURCE_GROUP("Source Files" FILES ${ezsat_SOURCE})
SOURCE_GROUP("Header Files" FILES ${ezsat_HEADER})

ADD_LIBRARY(ezsat ${ezsat_SOURCE} ${ezsat_HEADER})
