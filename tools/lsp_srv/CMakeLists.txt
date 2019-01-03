
#===============#
#     ydshd     #
#===============#

set(LSP_SRV ydshd)
add_executable(${LSP_SRV}
        main.cpp transport.cpp srv.cpp lsp.cpp
)
target_link_libraries(${LSP_SRV} ${YDSH_LIB} jsonrpc uri)