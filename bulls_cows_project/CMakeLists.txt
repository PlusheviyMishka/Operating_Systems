cmake_minimum_required(VERSION 3.10)

project(BullsAndCows C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Сервер
add_executable(server server.c)
target_link_libraries(server pthread)

# Клиент
add_executable(client client.c)