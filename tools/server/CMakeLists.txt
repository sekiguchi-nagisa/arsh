
#===============#
#     ydshd     #
#===============#

set(LSP_SRV ydshd)
add_library(ydshd_static STATIC
        transport.cpp
        server.cpp
        lsp.cpp)

add_executable(${LSP_SRV} main.cpp)
target_link_libraries(${LSP_SRV} ydshd_static ${YDSH_LIB} jsonrpc uri)

#install(TARGETS ${LSP_SRV}
#        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
#        LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})