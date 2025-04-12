cmake_minimum_required(VERSION 3.18)
project(YourProjectName LANGUAGES CXX CUDA)

# Set CUDA toolkit path explicitly
# set(CUDA_TOOLKIT_ROOT_DIR "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8")
# set(CMAKE_CUDA_COMPILER "${CUDA_TOOLKIT_ROOT_DIR}/bin/nvcc.exe")

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Set CUDA configurations
set(CMAKE_CUDA_STANDARD 20)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_ARCHITECTURES 75 80 86)
set(CMAKE_CUDA_SEPARABLE_COMPILATION ON)


set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Define paths
# set(NVENC_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/Video_Codec_SDK_13.0.19")

# Add message about NVIDIA SDK
message(STATUS "Using NVIDIA Video Codec SDK from: ${NVENC_SDK_DIR}")
# message(STATUS "Using CUDA from: ${CUDA_TOOLKIT_ROOT_DIR}")
message(STATUS "This software contains source code provided by NVIDIA Corporation.")

if(NOT DEFINED NVENC_SDK_DIR)
  message(FATAL_ERROR "NVENC_SDK_DIR must be defined. Please specify the path to the NVIDIA Encoder SDK directory.")
endif()

# Validate that the directory actually exists
if(NOT EXISTS "${NVENC_SDK_DIR}")
  message(FATAL_ERROR "NVENC_SDK_DIR points to a non-existent directory: ${NVENC_SDK_DIR}")
endif()

# Find required packages
find_package(CUDA REQUIRED)
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
	# "C:/Program Files/NVIDIA GPU Computing Toolkit/CUDA/v12.8/include"
    ${NVENC_SDK_DIR}/Interface
    # ${CUDA_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/external
    # "${CUDA_TOOLKIT_ROOT_DIR}/include"
)

# Set compile definitions
target_compile_definitions(${PROJECT_NAME} PRIVATE
    NOMINMAX  # Prevents Windows.h from defining min/max macros
)

# Link libraries (using modern target-based approach)
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE
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
