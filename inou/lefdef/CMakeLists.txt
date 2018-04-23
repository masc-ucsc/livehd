project(inou_lefdef)

FILE(GLOB inou_lefdef_SOURCE *.cpp)
FILE(GLOB inou_lefdef_HEADER *.hpp)

SET(EXELIST "lglefdef" "lefrw" "defrw")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR} ${misc_lef_SOURCE_DIR} ${misc_def_SOURCE_DIR})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM inou_lefdef_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

ADD_LIBRARY(inou_lefdef ${inou_lefdef_SOURCE} ${inou_lefdef_HEADER})

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} inou_lefdef inou_json lef def core)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread ${REQUIRED_LIBS})
ENDFOREACH(EXE)

