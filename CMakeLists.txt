# ./CMakeLists.txt (根目录)

cmake_minimum_required(VERSION 3.16)
project(mprpc)

# 1. 基础设置
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
add_definitions(-DSPDLOG_HEADER_ONLY)

# 设置库文件输出目录到根目录的 lib 文件夹
set(LIBRARY_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/lib)
# 设置可执行文件输出目录到根目录的 bin 文件夹
set(EXECUTABLE_OUTPUT_PATH ${PROJECT_SOURCE_DIR}/bin)

# 2. 查找必要的依赖
# 这些依赖通常整个项目都要用，所以在根目录查找
find_package(Threads REQUIRED) # RPC通常需要线程库
find_package(Protobuf REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

# 3. 添加子目录 proto
# 注意顺序：先编译 proto，因为 src 里的代码依赖 proto 生成的头文件
add_subdirectory(proto)
add_subdirectory(src)
add_subdirectory(test)

