#=========================================#
#     generate regex JIT stencil data     #
#=========================================#
#
# `USE_REGEX_JIT` is decided by the probe in the top-level `CMakeLists.txt`. when the JIT is off,
# this directory is a no-op so that the interpreter-only build is unaffected.
#
# the stencils are compiled as a standalone step: they are never linked into a library. they are
# turned into machine code with `-fno-pic -fno-pie -mcmodel=large` so that every reference to a
# control flow placeholder becomes an 8-byte absolute relocation (`R_X86_64_64`), which
# `gen_jit_stencil` records as a patch site. the resulting `stencil_data.h` is included by
# `src/regex/jit/jit_code.cpp`.

if (NOT USE_REGEX_JIT)
    return()
endif ()

add_executable(gen_jit_stencil
        main.cpp
        object_file_elf.cpp
        file_util.cpp
)
target_include_directories(gen_jit_stencil PRIVATE ${CMAKE_SOURCE_DIR}/src)

set(ARSH_JIT_STENCIL_SRC
        ${CMAKE_SOURCE_DIR}/src/regex/jit/stencil_basic.cpp
        ${CMAKE_SOURCE_DIR}/src/regex/jit/stencil_advanced.cpp
)

set(ARSH_JIT_STENCIL_OBJS "")
foreach (jit_src IN LISTS ARSH_JIT_STENCIL_SRC)
    get_filename_component(jit_name ${jit_src} NAME_WE)
    set(jit_obj ${CMAKE_CURRENT_BINARY_DIR}/${jit_name}.o)
    add_custom_command(OUTPUT ${jit_obj}
            COMMAND ${CMAKE_CXX_COMPILER}
            -std=gnu++17
            -O3
            -fno-pic -fno-pie
            -mcmodel=large
            -ffunction-sections -fdata-sections
            -fno-stack-protector
            -fno-asynchronous-unwind-tables -fno-unwind-tables
            -Wno-unused-command-line-argument
            "-DBEGIN_MISC_LIB_NAMESPACE_DECL=namespace arsh {"
            "-DEND_MISC_LIB_NAMESPACE_DECL=}"
            -D_GNU_SOURCE
            -I${CMAKE_SOURCE_DIR}/src
            -I${CMAKE_SOURCE_DIR}/include
            -I${CMAKE_BINARY_DIR}/src
            -c ${jit_src} -o ${jit_obj}
            MAIN_DEPENDENCY ${jit_src}
            DEPENDS ${CMAKE_SOURCE_DIR}/src/regex/instruction.h
            ${CMAKE_SOURCE_DIR}/src/regex/jit/stencil_context.h
            ${CMAKE_SOURCE_DIR}/src/regex/jit/stencil_runtime.h
            ${CMAKE_SOURCE_DIR}/src/regex/jit/jit_context.h
            ${CMAKE_SOURCE_DIR}/src/regex/jit/jit_abi.h
            COMMENT "compiling regex JIT stencil ${jit_name}.cpp"
            VERBATIM
    )
    list(APPEND ARSH_JIT_STENCIL_OBJS ${jit_obj})
endforeach ()

set(ARSH_JIT_STENCIL_DATA ${CMAKE_CURRENT_BINARY_DIR}/stencil_data.h)
add_custom_command(OUTPUT ${ARSH_JIT_STENCIL_DATA}
        COMMAND gen_jit_stencil --output ${ARSH_JIT_STENCIL_DATA} ${ARSH_JIT_STENCIL_OBJS}
        DEPENDS gen_jit_stencil ${ARSH_JIT_STENCIL_OBJS}
        COMMENT "generating regex JIT stencil data"
        VERBATIM
)

add_custom_target(regex_jit_stencil_data DEPENDS ${ARSH_JIT_STENCIL_DATA})

set(ARSH_JIT_STENCIL_DIR ${CMAKE_CURRENT_BINARY_DIR} PARENT_SCOPE)
