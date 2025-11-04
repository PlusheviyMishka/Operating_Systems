cmake_minimum_required(VERSION 3.10)
project(lab1 C)

# Настройка компилятора C
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(parent src/parent.c)
add_executable(child src/child.c)