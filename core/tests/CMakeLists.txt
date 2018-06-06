project(core_test)

FILE(GLOB core_test_SOURCE *.cpp)
FILE(GLOB core_test_HEADER *.hpp)

SET(TSTLIST "mmap_allocator" "mmap_vector" "dense_tst" "options_tst" "edge" "char_tst")
SET(EXELIST "iter")

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

FOREACH(EXE ${TSTLIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM core_test_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

FOREACH(EXE ${EXELIST})
	FILE(GLOB exec_SOURCE "${EXE}.cpp")
	ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
	LIST(REMOVE_ITEM core_test_SOURCE ${exec_SOURCE})
ENDFOREACH(EXE)

FOREACH(EXE ${TSTLIST})
  TARGET_LINK_LIBRARIES(${EXE} inou_rand inou_json core)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread)
ENDFOREACH(EXE)

FOREACH(EXE ${EXELIST})
  TARGET_LINK_LIBRARIES(${EXE} inou_rand inou_json core)
  TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread)
ENDFOREACH(EXE)

add_test(NAME core_prep  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/prep.sh)
add_test(NAME core_done  COMMAND rm -rf core_test_lgdb)
FOREACH(EXE ${TSTLIST})
  add_test(NAME core_${EXE}        COMMAND ${PROJECT_BINARY_DIR}/${EXE})
  SET_TESTS_PROPERTIES(core_${EXE} PROPERTIES DEPENDS core_prep)
  SET_PROPERTY(TEST core_done      APPEND PROPERTY DEPENDS core_${EXE})
ENDFOREACH(EXE)

