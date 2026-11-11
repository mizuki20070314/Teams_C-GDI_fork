cmake_minimum_required(VERSION 3.24)

project(TeamsCGDI VERSION 0.1.0 LANGUAGES C)

set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

add_executable(TeamsCGDI WIN32
    src/main.c
    src/data.c
    src/ui.c
)

target_compile_definitions(TeamsCGDI PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WIN32_WINNT=0x0601
)

target_link_libraries(TeamsCGDI PRIVATE
    user32
    gdi32
    shell32
)

if(MSVC)
    target_compile_options(TeamsCGDI PRIVATE /W4 /utf-8)
else()
    target_compile_options(TeamsCGDI PRIVATE -Wall -Wextra -Wpedantic -finput-charset=UTF-8)
endif()
