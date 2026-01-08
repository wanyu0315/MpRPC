cmake_minimum_required(VERSION 3.16)
project(rpc_integration_test)
set(CMAKE_CXX_STANDARD 17)

# 1. 查找依赖 (模拟用户环境)
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

# 2. 包含头文件 (这一步其实可选，因为 /usr/local/include 默认就在搜索路径)
# 但写上显得更规范，模拟用户显式指定路径
include_directories(/usr/local/include)
include_directories(${CMAKE_CURRENT_BINARY_DIR}) # 告诉编译器去构建目录(build)下找 user.pb.h

# 3. 处理 Proto
file(GLOB PROTO_FILES "*.proto")
protobuf_generate_cpp(PROTO_GEN_SRCS PROTO_GEN_HDRS ${PROTO_FILES})

# 4. 链接库目录 (同上，/usr/local/lib 默认在搜索路径，可选)
link_directories(/usr/local/lib)

# 5. 生成 Provider
add_executable(provider provider.cpp ${PROTO_GEN_SRCS})
# 重点：链接 mprpc 和所有依赖
target_link_libraries(provider 
    mprpc 
    muduo_net muduo_base zookeeper_mt 
    fmt::fmt spdlog::spdlog pthread 
    ${Protobuf_LIBRARIES})

# 6. 生成 Consumer
add_executable(consumer consumer.cpp ${PROTO_GEN_SRCS})
target_link_libraries(consumer 
    mprpc 
    muduo_net muduo_base zookeeper_mt 
    fmt::fmt spdlog::spdlog pthread 
    ${Protobuf_LIBRARIES})