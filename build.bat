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