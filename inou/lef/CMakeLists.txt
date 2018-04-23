project(inou_lef)

FILE(GLOB inou_lef_SOURCE *.cpp)
FILE(GLOB inou_lef_HEADER *.hpp)

SET(EXELIST "lglef")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR} ${misc_lef_SOURCE_DIR} ${misc_def_SOURCE_DIR})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM inou_lef_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

ADD_LIBRARY(inou_lef ${inou_lef_SOURCE} ${inou_lef_HEADER})

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} inou_lef inou_json lef def core)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread ${REQUIRED_LIBS})
ENDFOREACH(EXE)

