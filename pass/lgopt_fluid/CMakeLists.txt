project(pass_lgopt_fluid)

FILE(GLOB pass_lgopt_fluid_SOURCE *.cpp)
FILE(GLOB pass_lgopt_fluid_HEADER *.hpp)

SET(EXELIST "lgopt_fluid")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM pass_lgopt_fluid_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} pass_fluid core bitscan)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread ${REQUIRED_LIBS})
ENDFOREACH(EXE)

add_custom_command(TARGET lgopt_fluid PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/tests ${PROJECT_BINARY_DIR}/tests/)

