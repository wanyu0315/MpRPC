# ./test/CMakeLists.txt

#=========================================================
# 测试mprpcapplication
#=========================================================
# 1. 生成测试可执行文件
add_executable(test_app test_mprpcapplication.cpp)

# 2. 链接核心库 mprpc 与其他依赖库
# 这里的 mprpc 对应 src/CMakeLists.txt 里 add_library 的名字
target_link_libraries(test_app mprpc pthread fmt::fmt spdlog::spdlog)


#=========================================================
# 测试 Zookeeper 相关功能
#=========================================================

include_directories(${PROJECT_SOURCE_DIR}/src/include)
add_executable(test_zk test_zk.cpp)
target_link_libraries(test_zk mprpc zookeeper_mt pthread)

#=========================================================
# 测试 spdlog 日志库功能
#=========================================================
include_directories(${PROJECT_SOURCE_DIR}/src/include)
add_executable(test_spdlog test_spdlog.cpp)
target_link_libraries(test_spdlog mprpc fmt::fmt spdlog::spdlog pthread)