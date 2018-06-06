project(inou_sat)

FILE(GLOB inou_sat_SOURCE *.cpp)
FILE(GLOB inou_sat_HEADER *.hpp)


SET(EXELIST "lgsat_multibit")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM inou_sat_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

ADD_LIBRARY(inou_sat ${inou_sat_SOURCE} ${inou_sat_HEADER})



FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE}  inou_sat ezsat core)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread -lcryptominisat5 ${REQUIRED_LIBS})
ENDFOREACH(EXE)

