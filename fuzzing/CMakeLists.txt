#=========================#
#     clone libFuzzer     #
#=========================#

set(fuzzer_dir ${CMAKE_CURRENT_BINARY_DIR}/fuzzer)
add_custom_command(OUTPUT ${fuzzer_dir}
        COMMAND git clone https://chromium.googlesource.com/chromium/llvm-project/compiler-rt/lib/fuzzer
)
add_custom_target(fuzzer_dir DEPENDS ${fuzzer_dir})


#=========================#
#     build libFuzzer     #
#=========================#

set(fuzzer_static ${CMAKE_CURRENT_BINARY_DIR}/libFuzzer.a)
add_custom_command(OUTPUT ${fuzzer_static}
        COMMAND fuzzer/build.sh
)
add_custom_target(libFuzzer DEPENDS ${fuzzer_static})
add_dependencies(libFuzzer fuzzer_dir)


#===================#
#     ds_fuzzer     #
#===================#

add_executable(ds_fuzzer ds_fuzzer.cpp)
add_dependencies(ds_fuzzer libFuzzer)
target_link_libraries(ds_fuzzer ${YDSH_STATIC} ${fuzzer_static} pthread)
