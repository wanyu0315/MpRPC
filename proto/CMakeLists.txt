# proto/CMakeLists.txt

# 自动查找当前目录下的所有 .proto 文件
file(GLOB PROTO_FILES "*.proto")

# 调用 protoc 自动生成 .pb.cc 和 .pb.h
# 生成的文件通常会放在 build 目录下，保持源码目录干净
protobuf_generate_cpp(PROTO_GEN_SRCS PROTO_GEN_HDRS ${PROTO_FILES})

# 定义库目标
add_library(rpc_proto_lib ${PROTO_GEN_SRCS} ${PROTO_GEN_HDRS})

set_target_properties(rpc_proto_lib PROPERTIES POSITION_INDEPENDENT_CODE ON)

# 链接依赖
target_link_libraries(rpc_proto_lib PUBLIC ${Protobuf_LIBRARIES})

# 导出头文件路径 (当前 Binary 目录即 build/proto)，这里的CMAKE_CURRENT_BINARY_DIR是构建目录，在build下。
# rpc_proto_lib 说："谁链接我，谁就必须把 build/proto 加入头文件搜索路径"
target_include_directories(rpc_proto_lib PUBLIC ${CMAKE_CURRENT_BINARY_DIR})


# 安装部署 RPC Proto 库和头文件
# 1. 安装库文件 (librpc_proto_lib.a) 到 /usr/local/lib
install(TARGETS rpc_proto_lib DESTINATION lib)

# 2. 安装生成的头文件 (*.pb.h) 到 /usr/local/include/mprpc
# 注意：生成的头文件在构建目录(BINARY_DIR)下
install(FILES ${PROTO_GEN_HDRS} DESTINATION include/mprpc)