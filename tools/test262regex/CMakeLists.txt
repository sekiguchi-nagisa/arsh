#===============#
#     re262     #
#===============#

add_library(re262_static STATIC
        js.cpp
        meta.cpp
)
target_link_libraries(re262_static regex)

add_executable(re262 main.cpp)
target_link_libraries(re262 re262_static regex)