cmake_minimum_required(VERSION 3.14)

project(
    GC1
    LANGUAGES ASM C
)
add_compile_options(-fno-omit-frame-pointer)
add_subdirectory(src)
add_subdirectory(test)
