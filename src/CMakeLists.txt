# ./src/CMakeLists.txt (src目录)

# 扫描源文件
# 扫描 src 目录下所有的 .cpp 文件
aux_source_directory(. SRC_LIST)

# 生成核心库 (libmprpc.a)
add_library(mprpc STATIC ${SRC_LIST})

#  包含头文件路径
# 让代码能找到 src/include 下的头文件 (如 rpcprovider.h)
target_include_directories(mprpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# 链接依赖
# 链接在 proto 目录生成的库，以及线程库和 protobuf 库
target_link_libraries(mprpc 
    rpc_proto_lib 
    muduo_net  
    muduo_base  
    Threads::Threads 
    ${Protobuf_LIBRARIES}
    zookeeper_mt
    fmt::fmt 
    spdlog::spdlog
)


# 安装部署核心库和头文件
# 1. 安装静态库 (libmprpc.a) 到 /usr/local/lib
install(TARGETS mprpc DESTINATION lib)

# 安装头文件到 <prefix>/include
# 将 include 目录下的所有 .h 文件拷贝到 /usr/local/include/mprpc
install(DIRECTORY include/ DESTINATION include/mprpc FILES_MATCHING PATTERN "*.h")