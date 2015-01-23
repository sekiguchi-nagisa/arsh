string(TOLOWER "${CMAKE_BUILD_TYPE}" btype)
IF("${btype}" STREQUAL "debug")
    MESSAGE(WARNING "include debug.cpp, now enable debug logging")
    SET(YDSH_SRC
        ${YDSH_SRC}
        ${CMAKE_CURRENT_SOURCE_DIR}/debug.cpp
        PARENT_SCOPE
    )
ENDIF()
