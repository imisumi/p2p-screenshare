cmake_minimum_required(VERSION 3.14)
project(test_p2p VERSION 1.0.0 LANGUAGES CXX)

# Set C++20 standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Find GameNetworkingSockets package using vcpkg
find_package(GameNetworkingSockets CONFIG REQUIRED)

# Find all cpp files recursively in src directory
file(GLOB_RECURSE SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

# Create the executable
add_executable(${PROJECT_NAME} ${SOURCES})

# Link against GameNetworkingSockets from vcpkg
find_package(GameNetworkingSockets CONFIG REQUIRED)
target_include_directories(${PROJECT_NAME} PRIVATE ${GameNetworkingSockets_INCLUDE_DIRS})
target_link_libraries(${PROJECT_NAME} PRIVATE GameNetworkingSockets::GameNetworkingSockets)
# find_package(GameNetworkingSockets 1.4.1 EXACT REQUIRED)
# target_link_libraries(${PROJECT_NAME} PRIVATE GameNetworkingSockets::GameNetworkingSockets)

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Add Windows socket library if needed
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

# Optional: Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Optional: Add compiler-specific flags
if(MSVC)
    target_compile_options(${PROJECT_NAME} PRIVATE /W4)
else()
    target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -Wpedantic)
endif()
