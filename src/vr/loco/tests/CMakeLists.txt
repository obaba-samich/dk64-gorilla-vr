cmake_minimum_required(VERSION 3.16)
project(gtl_tests C)

set(CMAKE_C_STANDARD 99)
add_executable(test_gtl test_gtl.c ../gtl.c)
target_compile_options(test_gtl PRIVATE -Wall -Wextra -Wno-unused-function)
target_link_libraries(test_gtl m)

enable_testing()
add_test(NAME gtl COMMAND test_gtl)
