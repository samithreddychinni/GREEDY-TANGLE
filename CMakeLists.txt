cmake_minimum_required(VERSION 3.16)
project(GreedyTangle VERSION 0.1.0 LANGUAGES CXX)

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find SDL2 and SDL2_ttf
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 REQUIRED sdl2)
pkg_check_modules(SDL2_TTF REQUIRED SDL2_ttf)

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
)

# Source files
set(SOURCES
    src/main.cpp
    src/GameEngine.cpp
    src/MathUtils.cpp
    src/MenuBar.cpp
    src/CPUController.cpp
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# FIX: Enable M_PI for MinGW/GCC
target_compile_definitions(${PROJECT_NAME} PRIVATE _USE_MATH_DEFINES)

# Link SDL2 and SDL2_ttf
# Added mingw32 and SDL2main for Windows entry-point compatibility
target_link_libraries(${PROJECT_NAME} 
    mingw32 
    SDL2main 
    ${SDL2_LIBRARIES} 
    ${SDL2_TTF_LIBRARIES}
)

# Compiler warnings
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic
)