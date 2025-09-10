cmake_minimum_required(VERSION 4.0)
project(FT4222Eeprom C)

set(CMAKE_C_STANDARD 23)

add_executable(FT4222Eeprom main.c)
target_link_libraries(FT4222Eeprom libft4222.so)
