project(inou_pyrope)

FILE(GLOB inou_pyrope_SOURCE *.cpp)
FILE(GLOB inou_pyrope_HEADER *.hpp)

SET(EXELIST "lg2prp")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM inou_pyrope_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

ADD_LIBRARY(inou_pyrope ${inou_pyrope_SOURCE} ${inou_pyrope_HEADER})

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} inou_pyrope core)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread ${REQUIRED_LIBS})
ENDFOREACH(EXE)

