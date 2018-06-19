project(misc_lef)

FILE(GLOB misc_lef_SOURCE *.cpp)
FILE(GLOB misc_lef_HEADER *.hpp)
FILE(GLOB bison_SOURCE *.y)

SET(BisonOutput ${CMAKE_CURRENT_BINARY_DIR}/lef.tab.cpp)
#BISON_TARGET(lef lef.y ${CMAKE_CURRENT_BINARY_DIR}/lef.tab.cpp)

IF(BISON_FOUND)
    ADD_CUSTOM_COMMAND(
      OUTPUT ${BisonOutput}
      COMMAND ${BISON_EXECUTABLE}
      ARGS  "--output=${BisonOutput}"
            "--defines"
            "--name-prefix=lefyy"
            "${CMAKE_CURRENT_SOURCE_DIR}/lef.y"
      COMMENT "Generating lef.tab.cpp"
    )
  SET_SOURCE_FILES_PROPERTIES("${BisonOutput}" PROPERTIES GENERATED TRUE)
ENDIF()

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR} ${misc_lef_SOURCE_DIR})

ADD_LIBRARY(lef ${misc_lef_SOURCE} ${misc_lef_HEADER} ${BisonOutput})
