#!/bin/sh
source .venv/bin/activate
conan profile detect
# if build directory does not exist, create it
if [ ! -d "build" ]; then
    mkdir build
fi
cd build
conan install .. --build missing -s build_type=Debug
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_TOOLCHAIN_FILE=Debug/generators/conan_toolchain.cmake
cmake --build . -j 8