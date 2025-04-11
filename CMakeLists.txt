
# cmake_minimum_required(VERSION 3.18)
# project(YourProjectName LANGUAGES CXX CUDA)

# # Set C++ standard
# set(CMAKE_CXX_STANDARD 23)
# set(CMAKE_CXX_STANDARD_REQUIRED ON)

# # Set CUDA configurations
# enable_language(CUDA)
# set(CMAKE_CUDA_STANDARD 20)
# set(CMAKE_CUDA_STANDARD_REQUIRED ON)
# set(CMAKE_CUDA_ARCHITECTURES 75 80 86)

# # Use a relative path to your vendor folder
# set(NVENC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Video_Codec_SDK_13.0.19")

# # Find CUDA package
# find_package(CUDA REQUIRED)

# # Include SDK and CUDA headers
# include_directories(
#     ${NVENC_SDK_DIR}/Interface
#     ${CUDA_INCLUDE_DIRS}
# )

# # Link directories
# link_directories(
#     ${NVENC_SDK_DIR}/Lib/x64
#     ${CUDA_TOOLKIT_ROOT_DIR}/lib/x64
# )

# # Find all source files recursively in the src directory
# # file(GLOB_RECURSE SOURCES "src/*.cpp" "src/*.c" "src/*.h" "src/*.hpp")
# # file(GLOB_RECURSE SOURCES "Decode/*.cpp" "Decode/*.c" "Decode/*.h" "Decode/*.hpp Decode/*.cu")
# # file(GLOB_RECURSE SOURCES "Decode/*.cpp" "Decode/*.c" "Decode/*.h" "Decode/*.hpp" "Decode/*.cu")
# file(GLOB_RECURSE SOURCES "Decode/*.cpp" "Decode/*.h" "Decode/*.cu")
# # file(GLOB_RECURSE SOURCES "test/*.cpp" "test/*.c" "test/*.h" "test/*.hpp")
# find_package(FFMPEG REQUIRED)
# include_directories(${FFMPEG_INCLUDE_DIRS})
# # Create executable
# add_executable(${PROJECT_NAME} ${SOURCES})

# # On Windows, you might need to specify the .lib files explicitly
# if(WIN32)
#     target_link_libraries(${PROJECT_NAME}
# 	${FFMPEG_LIBRARIES}
#         ${NVENC_SDK_DIR}/Lib/x64/nvencodeapi.lib
#         ${NVENC_SDK_DIR}/Lib/x64/nvcuvid.lib
#         cuda.lib
#         cudart.lib
# 		dxgi d3d11 d3d9 d3d12
# 		${CUDA_LIBRARIES}
# 		${CUDA_CUDA_LIBRARY}
# 		${CUDA_CUDART_LIBRARY}
#     )
# endif()

# set_target_properties(${PROJECT_NAME} PROPERTIES
#     CUDA_SEPARABLE_COMPILATION ON
# )

# # Add compile definitions if needed
# add_definitions(-DNOMINMAX) # Useful when using Windows.h with C++ std library

cmake_minimum_required(VERSION 3.18)
project(YourProjectName LANGUAGES CXX CUDA)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set CUDA configurations
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_ARCHITECTURES 75 80 86)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)

# Define paths
set(NVENC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Video_Codec_SDK_13.0.19")

# Add message about NVIDIA SDK
message(STATUS "Using NVIDIA Video Codec SDK from: ${NVENC_SDK_DIR}")
message(STATUS "This software contains source code provided by NVIDIA Corporation.")

# Find required packages
find_package(CUDA REQUIRED)
find_package(FFMPEG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# Collect source files
file(GLOB_RECURSE PROJECT_SOURCES 
    "src/*.cpp"
    "src/*.h"
    "src/*.cu"
)

# Create executable
add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

# Set include directories (use modern target-based approach)
target_include_directories(${PROJECT_NAME} PRIVATE
    ${NVENC_SDK_DIR}/Interface
    ${CUDA_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
	${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external
)

# Set compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX  # Prevents Windows.h from defining min/max macros
)

# Link libraries (using modern target-based approach)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        ${FFMPEG_LIBRARIES}
        ${CUDA_LIBRARIES}
        ${CUDA_CUDA_LIBRARY}
        ${CUDA_CUDART_LIBRARY}
        dxgi
        d3d11
        d3d9
        d3d12
		winmm
		spdlog::spdlog
    )
    
    # Link NVIDIA libraries
    if(EXISTS "${NVENC_SDK_DIR}/Lib/x64/nvencodeapi.lib")
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "${NVENC_SDK_DIR}/Lib/x64/nvencodeapi.lib"
        )
    else()
        message(FATAL_ERROR "NVIDIA NVENC API library not found at: ${NVENC_SDK_DIR}/Lib/x64/nvencodeapi.lib")
    endif()
    
    if(EXISTS "${NVENC_SDK_DIR}/Lib/x64/nvcuvid.lib")
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "${NVENC_SDK_DIR}/Lib/x64/nvcuvid.lib"
        )
    else()
        message(FATAL_ERROR "NVIDIA CUVID library not found at: ${NVENC_SDK_DIR}/Lib/x64/nvcuvid.lib")
    endif()
endif()

# Install targets (optional)
install(TARGETS ${PROJECT_NAME}
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)