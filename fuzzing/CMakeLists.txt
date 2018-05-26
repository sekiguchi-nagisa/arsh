#===================#
#     ds_fuzzer     #
#===================#

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fsanitize=fuzzer")
add_executable(ds_fuzzer ds_fuzzer.cpp)
target_link_libraries(ds_fuzzer ${YDSH_STATIC})
