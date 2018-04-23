project(inou_blif)

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})
INCLUDE_DIRECTORIES("${CMAKE_SOURCE_DIR}/misc/yosys")


FILE(GLOB inou_blif_SOURCE *.cpp)
FILE(GLOB inou_blif_HEADER *.hpp)

SET(EXELIST "lgblif")


FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM inou_blif_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)


ADD_LIBRARY(inou_blif ${inou_blif_SOURCE} ${inou_blif_HEADER})
TARGET_LINK_LIBRARIES(inou_blif core)
TARGET_LINK_LIBRARIES(inou_blif -Wl,-Bstatic ${BOOST_LIBS} -Wl,-Bdynamic -lrt -lpthread ${REQUIRED_LIBS})

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} inou_blif core)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread)
ENDFOREACH(EXE)