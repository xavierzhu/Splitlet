cmake_minimum_required(VERSION 3.20)

project(Splitlet LANGUAGES CXX)

option(SPLITLET_STATIC_RUNTIME "Build a self-contained executable with the MinGW runtime statically linked" ON)

add_executable(Splitlet WIN32
    src/app.rc
    src/main.cpp
)

target_compile_features(Splitlet PRIVATE cxx_std_17)
target_link_libraries(Splitlet PRIVATE shell32)

if(MINGW)
    target_compile_options(Splitlet PRIVATE -Wall -Wextra -Os -ffunction-sections -fdata-sections)
    target_link_options(Splitlet PRIVATE -municode -s -Wl,--gc-sections)
    if(SPLITLET_STATIC_RUNTIME)
        target_link_options(Splitlet PRIVATE -static -static-libgcc -static-libstdc++)
    endif()
endif()

if(MSVC)
    target_compile_options(Splitlet PRIVATE /W4 /O2)
endif()
