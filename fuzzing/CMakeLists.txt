#===================#
#     ds_fuzzer     #
#===================#

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=fuzzer")
add_executable(ds_fuzzer ds_fuzzer.cpp)
target_link_libraries(ds_fuzzer ${YDSH_LIB})


#====================#
#     rpc_fuzzer     #
#====================#

include_directories(${CMAKE_SOURCE_DIR}/tools/json)
add_executable(rpc_fuzzer rpc_fuzzer.cpp)
target_link_libraries(rpc_fuzzer jsonrpc)


#====================#
#     lsp_fuzzer     #
#====================#

include_directories(${CMAKE_SOURCE_DIR}/tools/analyzer)
add_executable(lsp_fuzzer lsp_fuzzer.cpp)
target_link_libraries(lsp_fuzzer ydshd_static ydsh_static)


#=====================#
#     comp_fuzzer     #
#=====================#

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=fuzzer")
add_executable(comp_fuzzer comp_fuzzer.cpp)
target_link_libraries(comp_fuzzer ${YDSH_LIB})