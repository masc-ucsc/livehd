project(inou_yaml)

FILE(GLOB inou_yaml_SOURCE *.cpp)
FILE(GLOB inou_yaml_HEADER *.hpp)

SET(EXELIST "lgyaml")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM inou_yaml_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

ADD_LIBRARY(inou_yaml ${inou_yaml_SOURCE} ${inou_yaml_HEADER})

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} inou_yaml core bitscan)
  TARGET_LINK_LIBRARIES(${EXE} -lyaml-cpp ${BOOST_LIBS} -lpthread)
ENDFOREACH(EXE)

