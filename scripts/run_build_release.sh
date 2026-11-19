#!/bin/sh
source .venv/bin/activate
conan profile detect
# if build directory does not exist, create it
if [ ! -d "build" ]; then
    mkdir build
fi
cd build
conan install .. --build missing -s build_type=Release
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_TOOLCHAIN_FILE=Release/generators/conan_toolchain.cmake
cmake --build . -j$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 8)
