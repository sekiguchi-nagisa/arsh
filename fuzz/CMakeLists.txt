#==========================#
#     create libFuzzer     #
#==========================#

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    # clone libFuzzer
    set(fuzzer_dir ${CMAKE_CURRENT_BINARY_DIR}/Fuzzer)
    add_custom_command(OUTPUT ${fuzzer_dir}
            COMMAND git clone https://chromium.googlesource.com/chromium/llvm-project/llvm/lib/Fuzzer
    )
    add_custom_target(fuzzer_dir DEPENDS ${fuzzer_dir})

    # build libFuzzer
    set(fuzzer_static ${CMAKE_CURRENT_BINARY_DIR}/libFuzzer.a)
    add_custom_command(OUTPUT ${fuzzer_static}
            COMMAND ${CMAKE_CXX_COMPILER} -c -g -O2 -std=c++11 Fuzzer/*.cpp -IFuzzer
            COMMAND ar ruv libFuzzer.a Fuzzer*.o
    )
    add_custom_target(libFuzzer DEPENDS ${fuzzer_static})
    add_dependencies(libFuzzer fuzzer_dir)

    # build fuzzing target
    set(CMAKE_BUILD_TYPE Debug)
    set(CMAKE_CXX_FLAGS "-fsanitize=address -fsanitize-coverage=edge,indirect-calls,8bit-counters -std=c++11")
    add_executable(lexer_fuzzer
            lexer_fuzzer.cpp
            ${lexer_src}
            ${CMAKE_SOURCE_DIR}/src/lexer.cpp
    )
    add_dependencies(lexer_fuzzer gen_lexer libFuzzer)
    target_link_libraries(lexer_fuzzer ${fuzzer_static})
endif()
