cmake_minimum_required(VERSION 3.10)
project(lab2 VERSION 1.0)

# Настройка стандарта C
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Все выходные файлы в bin/
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# 1. Библиотека 1 (прямоугольники + двоичная система)
add_library(impl1 SHARED
    ./src/integr_rect.c
    ./src/decim_to_two.c
)

# Линкуем с математической библиотекой
target_link_libraries(impl1 m)

# 2. Библиотека 2 (трапеции + троичная система)
add_library(impl2 SHARED
    ./src/integr_trapezoid.c  
    ./src/decim_to_three.c
)

# Линкуем с математической библиотекой
target_link_libraries(impl2 m)

# 3. Программа с компиляционным связыванием
add_executable(compile_time_prog
    ./src/compile_time_main.c
)

# Связываем с первой библиотекой
target_link_libraries(compile_time_prog impl1 m)

# 4. Программа с динамической загрузкой
add_executable(runtime_prog
    ./src/runtime_main.c
)

# Для динамической загрузки нужна библиотека dl
if(UNIX)
    target_link_libraries(runtime_prog dl)
endif()

# Инструкция по сборке и запуску
message(STATUS "==============================================")
message(STATUS "Инструкция по сборке:")
message(STATUS "  mkdir build && cd build")
message(STATUS "  cmake ..")
message(STATUS "  make")
message(STATUS "")
message(STATUS "Запуск программ:")
message(STATUS "  ./bin/compile_time_prog - программа с компиляционным связыванием")
message(STATUS "  cd bin && ./runtime_prog - программа с динамической загрузкой")
message(STATUS "")
message(STATUS "Все файлы будут в папке bin/:")
message(STATUS "  compile_time_prog")
message(STATUS "  runtime_prog")
message(STATUS "  libimpl1.so - прямоугольники + двоичная система")
message(STATUS "  libimpl2.so - трапеции + троичная система")
message(STATUS "==============================================")
