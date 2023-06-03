
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
        index.cpp
        indexer.cpp
        client.cpp
        driver.cpp
        symbol.cpp
        worker.cpp
        semantic_token.cpp
        pass.cpp
        extra_checker.cpp
        )
target_link_libraries(ydshd_static ydsh_static jsonrpc uri directive)

add_executable(${LSP_SRV} main.cpp)
target_link_libraries(${LSP_SRV} ydshd_static ydsh_static jsonrpc uri process highlighter Threads::Threads)

install(TARGETS ${LSP_SRV}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})