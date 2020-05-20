
#===============#
#     ydshd     #
#===============#

set(LSP_SRV_STATIC ydshd_static)
add_library(${LSP_SRV_STATIC} STATIC
        transport.cpp server.cpp lsp.cpp
)
target_link_libraries(${LSP_SRV_STATIC} ${YDSH_LIB} jsonrpc uri)

set(LSP_SRV ydshd)
add_executable(${LSP_SRV} main.cpp)
target_link_libraries(${LSP_SRV} ${LSP_SRV_STATIC})

#install(TARGETS ${LSP_SRV}
#        RUNTIME DESTINATION ${CMAKE_INSTALL_FULL_BINDIR}
#        LIBRARY DESTINATION ${CMAKE_INSTALL_FULL_LIBDIR})