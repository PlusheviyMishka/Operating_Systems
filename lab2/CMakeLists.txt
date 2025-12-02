cmake_minimum_required(VERSION 3.10)
project(lab2 C)

# Настройка компилятора C
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Поиск библиотеки pthread
find_package(Threads REQUIRED)

add_executable(naive_search src/naive_search.c)

# Связывание с библиотекой pthread
target_link_libraries(naive_search PRIVATE Threads::Threads)