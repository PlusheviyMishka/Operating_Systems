cmake_minimum_required(VERSION 3.10)
project(lab3)

add_executable(parent ./src/parent.c)
add_executable(child ./src/child.c)

target_compile_features(parent PRIVATE c_std_11)
target_compile_features(child PRIVATE c_std_11)

target_compile_options(parent PRIVATE -Wall -Wextra -pthread)
target_compile_options(child PRIVATE -Wall -Wextra -pthread)

target_link_libraries(parent pthread rt)
target_link_libraries(child pthread rt)