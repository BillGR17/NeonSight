#!/usr/bin/env bash
set -e

CLEAN_BUILD=false
BUILD_TYPE="Debug"

for arg in "$@"; do
  case $arg in
    -n)
      CLEAN_BUILD=true
      ;;
    -r)
      BUILD_TYPE="Release"
      ;;
  esac
done

if [ "$CLEAN_BUILD" = true ]; then
  echo "Cleaning existing build directory..."
  rm -rf build
fi

echo "Building NeonSight ($BUILD_TYPE)..."

# Ensure build directory exists
mkdir -p build
cd build

# Run CMake and Make
cmake -DCMAKE_BUILD_TYPE=$BUILD_TYPE ..
cmake --build . --parallel $(nproc)

echo "Build complete. Executable is located at: build/neonsight"
