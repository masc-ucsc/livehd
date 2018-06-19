project(pass_lgopt)

FILE(GLOB pass_lgopt_SOURCE *.cpp)
FILE(GLOB pass_lgopt_HEADER *.hpp)

SET(EXELIST "lgopt")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM pass_lgopt_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} pass_dce pass_cse pass_vectorize core pass_sample)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread ${REQUIRED_LIBS})
ENDFOREACH(EXE)

