
#===============#
#     ydshd     #
#===============#

set(LSP_SRV ydshd)
add_executable(${LSP_SRV}
        main.cpp transport.cpp srv.cpp
)
target_link_libraries(${LSP_SRV} ${YDSH_LIB} jsonrpc)