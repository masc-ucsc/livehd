project(inou_abc)

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})
INCLUDE_DIRECTORIES("${CMAKE_SOURCE_DIR}/subs/yosys")
INCLUDE_DIRECTORIES("${CMAKE_SOURCE_DIR}/subs/yosys/abc/src")


FILE(GLOB inou_abc_SOURCE *.cpp)
FILE(GLOB inou_abc_HEADER *.hpp)

SET(EXELIST "lgaig")


FOREACH (EXE ${EXELIST})
    FILE(GLOB exec_SOURCE "${EXE}.cpp")
    ADD_EXECUTABLE(${EXE} ${exec_SOURCE})
    LIST(REMOVE_ITEM inou_abc_SOURCE ${exec_SOURCE})
ENDFOREACH (EXE)


ExternalProject_Add(libabc
  SOURCE_DIR ${PROJECT_SOURCE_DIR}/subs/yosys/abc
  CONFIGURE_COMMAND ""
  BUILD_COMMAND make -C ${CMAKE_SOURCE_DIR}/subs/yosys/abc libabc.a ABC_USE_PIC=true
  BUILD_IN_SOURCE 0
  DOWNLOAD_COMMAND ""
  INSTALL_COMMAND ""
  )
add_dependencies(libabc yosys)

ADD_LIBRARY(inou_abc ${inou_abc_SOURCE} ${inou_abc_HEADER})
add_dependencies(inou_abc libabc)
TARGET_LINK_LIBRARIES(inou_abc core ${CMAKE_SOURCE_DIR}/subs/yosys/abc/libabc.a)
TARGET_LINK_LIBRARIES(inou_abc -Wl,-Bstatic ${BOOST_LIBS} -Wl,-Bdynamic -lrt -lpthread ${REQUIRED_LIBS})
TARGET_LINK_LIBRARIES(inou_abc -lreadline -lpthread -ldl)

add_custom_command(TARGET inou_abc PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/abc_check ${PROJECT_BINARY_DIR}/abc_check)


FOREACH (EXE ${EXELIST})
    TARGET_LINK_LIBRARIES(${EXE} inou_abc core inou_lef lef def)
    TARGET_LINK_LIBRARIES(${EXE} ${BOOST_LIBS} -lpthread)
ENDFOREACH (EXE)
