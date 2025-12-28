#=========================================================
# 测试mprpcapplication
#=========================================================
# 1. 生成测试可执行文件
add_executable(test_app test_mprpcapplication.cpp)

# 2. 链接核心库 mprpc
# 这里的 mprpc 对应 src/CMakeLists.txt 里 add_library 的名字
target_link_libraries(test_app mprpc)

# 3. 添加线程库支持 (C++11 信号处理和原子变量可能需要)
target_link_libraries(test_app pthread)


#=========================================================
# 测试 Zookeeper 相关功能
#=========================================================

include_directories(${PROJECT_SOURCE_DIR}/src/include)
add_executable(test_zk test_zk.cpp)
target_link_libraries(test_zk mprpc zookeeper_mt pthread)