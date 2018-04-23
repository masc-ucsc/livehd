project(misc_def)

FILE(GLOB misc_def_SOURCE *.cpp)
FILE(GLOB misc_def_HEADER *.hpp)
FILE(GLOB bison_SOURCE *.y)

SET(BisonOutput ${CMAKE_CURRENT_BINARY_DIR}/def.tab.cpp)
#BISON_TARGET(def def.y ${CMAKE_CURRENT_BINARY_DIR}/def.tab.cpp)

IF(BISON_FOUND)
    ADD_CUSTOM_COMMAND(
      OUTPUT ${BisonOutput}
      COMMAND ${BISON_EXECUTABLE}
      ARGS  "--output=${BisonOutput}"
            "--defines"
            "--name-prefix=defyy"
            "${CMAKE_CURRENT_SOURCE_DIR}/def.y"
      COMMENT "Generating def.tab.cpp"
    )
  SET_SOURCE_FILES_PROPERTIES("${BisonOutput}" PROPERTIES GENERATED TRUE)
ENDIF()

INCLUDE_DIRECTORIES(${CMAKE_CURRENT_BINARY_DIR} ${misc_def_SOURCE_DIR})

ADD_LIBRARY(def ${misc_def_SOURCE} ${misc_def_HEADER})
