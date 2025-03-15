@REM @echo off

@REM :: Set OpenSSL environment variable
@REM set OPENSSL_ROOT_DIR=C:\Program Files\OpenSSL-Win64

@REM :: Create build directory if it doesn't exist
@REM if not exist build mkdir build

@REM :: Change to build directory
@REM cd build

@REM :: Configure CMake with Visual Studio 2022 and x64, using vcpkg toolchain
@REM cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake"

@REM :: Build the project
@REM cmake --build . --config Debug

@REM :: Return to original directory
@REM cd .. 

@echo off
@REM OPENSSL_ROOT_DIR=C:\Program Files\OpenSSL-Win64

:: Create build directory if it doesn't exist
if not exist build mkdir build

:: Change to build directory
cd build

:: Configure CMake with Visual Studio 2022 and x64, using vcpkg toolchain
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake"

:: Build the project
cmake --build . --config Debug
@REM cpack -C Release

:: Return to original directory
cd .. 