cmake_minimum_required(VERSION 3.16)
project(GreedyTangle VERSION 0.2.0 LANGUAGES CXX)

# C++20 Standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find SDL2 and SDL2_ttf
find_package(PkgConfig REQUIRED)
pkg_check_modules(SDL2 sdl2)
pkg_check_modules(SDL2_TTF SDL2_ttf)

# Fallback finding if pkg-config fails (common on Windows)
if(NOT SDL2_FOUND)
    find_package(SDL2 REQUIRED)
endif()
if(NOT SDL2_TTF_FOUND)
    find_package(SDL2_TTF REQUIRED)
endif()

# Include directories
include_directories(
    ${PROJECT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIR} # From find_package
    ${SDL2_TTF_INCLUDE_DIR}
)

# Source files
set(SOURCES
    src/main.cpp
    src/GameEngine.cpp
    src/MathUtils.cpp
    src/MenuBar.cpp
    src/CPUController.cpp
    src/GreedySolver.cpp
    src/SolverFactory.cpp
    src/DnCDPSolver.cpp
)

# Executable
add_executable(${PROJECT_NAME} ${SOURCES})

# FIX: Enable M_PI for MinGW/GCC
target_compile_definitions(${PROJECT_NAME} PRIVATE _USE_MATH_DEFINES)

# Link SDL2 and SDL2_ttf
target_link_libraries(${PROJECT_NAME} 
    ${SDL2_LIBRARIES} 
    ${SDL2_TTF_LIBRARIES}
)

if(WIN32)
    target_link_libraries(${PROJECT_NAME} 
        mingw32 
        SDL2main
    )
endif()

# Compiler warnings
target_compile_options(${PROJECT_NAME} PRIVATE
    -Wall -Wextra -Wpedantic
)