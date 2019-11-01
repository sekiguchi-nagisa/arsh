#=================#
#     inspect     #
#=================#

add_executable(inspect main.cpp ../../src/signals.cpp)
target_link_libraries(inspect platform)