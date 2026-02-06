cmake -DCMAKE_BUILD_TYPE=Debug  -G "Visual Studio 17 2022" -S . -B build  -DCMAKE_TOOLCHAIN_FILE=E:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --config Debug -v

@REM vcpkg ffmpeg 定制:
@REM 复制 ffmpeg ports 文件夹 到 custom-ports
@REM 修改 portfile.cmake 添加自定义配置, 如先添加 --disable-everything 
@REM 
@REM 编译:
@REM 在 CMakeLists.txt 中（要在 project() 之前）：
@REM set(VCPKG_OVERLAY_PORTS "${CMAKE_CURRENT_SOURCE_DIR}/custom-ports/ffmpeg")
@REM 或者
@REM E:\vcpkg\vcpkg install --overlay-ports=./custom-ports/ffmpe