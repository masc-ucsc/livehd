project(inou_yosys)

FILE(GLOB inou_yosys_SOURCE *.cpp)
FILE(GLOB inou_yosys_HEADER *.hpp)

INCLUDE_DIRECTORIES(${core_SOURCE_DIR})

ADD_LIBRARY(inou_yosys SHARED ${inou_yosys_SOURCE} ${inou_yosys_HEADER})

set (YOSYS_CONFIG_CXX yosys-config --cxxflags)
set (YOSYS_CONFIG_LDF yosys-config --ldflags --shared --ldlibs)

execute_process(COMMAND ${YOSYS_CONFIG_CXX} OUTPUT_VARIABLE YOSYS_CXX OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND ${YOSYS_CONFIG_LDF} OUTPUT_VARIABLE YOSYS_LDF OUTPUT_STRIP_TRAILING_WHITESPACE)

SET(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} ${YOSYS_LDF}")

include_directories("${CMAKE_SOURCE_DIR}/subs/yosys")
TARGET_LINK_LIBRARIES(inou_yosys core)
TARGET_LINK_LIBRARIES(inou_yosys -Wl,-Bstatic ${BOOST_LIBS} -Wl,-Bdynamic -lrt -lpthread ${REQUIRED_LIBS})

add_custom_command(TARGET inou_yosys PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/subs/yosys/techlibs/common/simcells.v ${PROJECT_BINARY_DIR}/simcell.v)
add_custom_command(TARGET inou_yosys PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/lgyosys ${PROJECT_BINARY_DIR}/lgyosys)
add_custom_command(TARGET inou_yosys PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/lgcheck ${PROJECT_BINARY_DIR}/lgcheck)
add_custom_command(TARGET inou_yosys PRE_BUILD COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/tests ${PROJECT_BINARY_DIR}/tests/)

add_test(NAME extra_inou_yosys_parse COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/lgyosys --testdir=${CMAKE_CURRENT_SOURCE_DIR}/tests
  --logdir=${PROJECT_BINARY_DIR}/../../logs
  --lgdb=${PROJECT_BINARY_DIR}/../../lgdb
  --binary=${PROJECT_BINARY_DIR}/../../subs/yosys/bin/yosys
  --inou=${PROJECT_BINARY_DIR}/libinou_yosys.so CONFIGURATIONS complete)

