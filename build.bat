:: Create build directory if it doesn't exist
if not exist build mkdir build

:: Change to build directory
cd build

:: Configure CMake with Visual Studio 2022 and x64, using vcpkg toolchain
cmake .. -DCMAKE_TOOLCHAIN_FILE="C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" -DNVENC_SDK_DIR="C:/dev/nvidia-sdk-test/nvenc-implenentation/p2p-screenshare/vendor/Video_Codec_SDK_13.0.19"
@REM cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE="C:/dev/vcpkg/scripts/buildsystems/vcpkg.cmake" -DNVENC_SDK_DIR="C:/dev/nvidia-sdk-test/nvenc-implenentation/p2p-screenshare/vendor/Video_Codec_SDK_13.0.19"
@REM cmake .. -G "Visual Studio 17 2022" -A x64

:: Build the project
cmake --build . --config Debug
@REM cpack -C Release

:: Return to original directory
cd .. 