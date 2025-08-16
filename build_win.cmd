cmake -DCMAKE_BUILD_TYPE=Debug  -G "Visual Studio 17 2022" -S . -B build  -DCMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug -v
