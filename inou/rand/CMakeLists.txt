project(inou_rand)

FILE(GLOB inou_rand_SOURCE *.cpp)
FILE(GLOB inou_rand_HEADER *.hpp)

SET(EXELIST "lgrand")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM inou_rand_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

ADD_LIBRARY(inou_rand ${inou_rand_SOURCE} ${inou_rand_HEADER})

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} inou_rand inou_json core)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread)
ENDFOREACH(EXE)

