cmake_minimum_required(VERSION 3.20)

project(Splitlet LANGUAGES CXX)

add_executable(Splitlet WIN32
    src/app.rc
    src/main.cpp
)

target_compile_features(Splitlet PRIVATE cxx_std_17)
target_link_libraries(Splitlet PRIVATE shell32)

if(MINGW)
    target_compile_options(Splitlet PRIVATE -Wall -Wextra -Os)
    target_link_options(Splitlet PRIVATE -municode -static -static-libgcc -static-libstdc++ -s)
endif()

if(MSVC)
    target_compile_options(Splitlet PRIVATE /W4 /O2)
endif()
