project(inou_tech)

FILE(GLOB inou_tech_SOURCE *.cpp)
FILE(GLOB inou_tech_HEADER *.hpp)

SET(EXELIST "import_library")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
ENDFOREACH(EXE)

ADD_LIBRARY(inou_tech ${inou_tech_SOURCE} ${inou_tech_HEADER})

add_custom_command(TARGET inou_tech PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/*.rb ${PROJECT_BINARY_DIR}/)
add_custom_command(TARGET inou_tech PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/func_liberty_json.sh ${PROJECT_BINARY_DIR}/)
add_custom_command(TARGET inou_tech PRE_BUILD COMMAND ${CMAKE_COMMAND} -E echo "${CMAKE_CURRENT_SOURCE_DIR}" > ${PROJECT_BINARY_DIR}/src)

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} inou_tech core)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread)
ENDFOREACH(EXE)

