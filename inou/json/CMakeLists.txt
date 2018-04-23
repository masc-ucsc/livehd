project(inou_json)

FILE(GLOB inou_json_SOURCE *.cpp)
FILE(GLOB inou_json_HEADER *.hpp)

SET(EXELIST "lgjson")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM inou_json_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

ADD_LIBRARY(inou_json ${inou_json_SOURCE} ${inou_json_HEADER})

add_custom_command(TARGET inou_json PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${core_SOURCE_DIR}/../misc/lgvisual/lgvisual.py ${PROJECT_BINARY_DIR}/lgvisual.py)
FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} inou_json inou_rand core bitscan)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread)
ENDFOREACH(EXE)

