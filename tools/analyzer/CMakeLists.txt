
#===============#
#     ydshd     #
#===============#

set(LSP_SRV ydshd)
add_library(ydshd_static STATIC
        transport.cpp
        server.cpp
        lsp.cpp
        analyzer.cpp
        archive.cpp
        source.cpp
        symbol.cpp
        symbol_builder.cpp
        )
target_link_libraries(ydshd_static ydsh_static jsonrpc uri)

add_executable(${LSP_SRV} main.cpp)
target_link_libraries(${LSP_SRV} ydshd_static ydsh_static jsonrpc uri)

install(TARGETS ${LSP_SRV}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})