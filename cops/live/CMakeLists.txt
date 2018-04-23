project(cops_live)

FILE(GLOB cops_live_SOURCE *.cpp)
FILE(GLOB cops_live_HEADER *.hpp)

SET(EXELIST "lginvariant" "lglive" "lgstitch")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

ADD_LIBRARY(cops_live ${cops_live_SOURCE} ${cops_live_HEADER})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM cops_live_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} cops_live core bitscan)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread ${REQUIRED_LIBS})
ENDFOREACH(EXE)

add_custom_command(TARGET cops_live PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/lgsetup ${PROJECT_BINARY_DIR}/lgsetup)
add_custom_command(TARGET cops_live PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/lglivesynth ${PROJECT_BINARY_DIR}/lglivesynth)
