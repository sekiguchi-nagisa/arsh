
#============================#
#     gen_casefold_table     #
#============================#

add_library(fold_table STATIC fold_table.cpp)

add_executable(gen_casefold_table main.cpp)
target_link_libraries(gen_casefold_table fold_table)