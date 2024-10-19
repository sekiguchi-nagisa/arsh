
#=======================================#
#     generate embedded sample code     #
#=======================================#

set(sample_src ${CMAKE_CURRENT_BINARY_DIR}/sample.h)
add_custom_command(OUTPUT ${sample_src}
        COMMAND ${CMAKE_BINARY_DIR}/tools/src_to_str/src_to_str
        -n -f ${CMAKE_SOURCE_DIR}/package/gen_asset.arsh
        -o ${sample_src} -v highlight_sample
        MAIN_DEPENDENCY ${CMAKE_SOURCE_DIR}/package/gen_asset.arsh
        DEPENDS src_to_str
)

#=====================#
#     highlighter     #
#=====================#

include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CMAKE_CURRENT_BINARY_DIR})
add_library(highlighter STATIC
        style.cpp
        formatter.cpp
        factory.cpp
        ${sample_src})
target_link_libraries(highlighter arsh_static)

set(HIGHLIGHTER arcolorize)
add_executable(${HIGHLIGHTER} main.cpp)
target_link_libraries(${HIGHLIGHTER} highlighter arsh_static)

install(TARGETS ${HIGHLIGHTER}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install_symlink_bindir(${HIGHLIGHTER} "dscolorize")