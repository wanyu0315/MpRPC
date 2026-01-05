// test_spdlog.cpp
#include "mprpcapplication.h"
#include <thread>

void test_log_concurrency() {
    for (int i = 0; i < 50; ++i) {
        LOG_INFO("Thread log test index: {}", i);
    }
}

int main(int argc, char* argv[]) {
    // 1. 初始化框架（会顺带初始化日志系统）
    // 伪造一下命令行参数，或者让 Init 内部读默认配置
    MprpcApplication::Init(argc, argv);

    // 2. 测试基本输出
    LOG_INFO("========== Testing Basic Log ==========");
    LOG_INFO("Hello, Mprpc! Port: {}", 8080);
    LOG_WARN("This is a warning message.");
    LOG_ERROR("This is an error message with arg: {}", "ErrorDetails");
    
    // 3. 测试日志级别过滤
    // 假设配置文件或者 Init 中默认是 INFO
    LOG_DEBUG("This debug message should NOT appear if level is INFO"); 

    // 4. 测试多线程并发写日志 (验证是否线程安全/乱序)
    LOG_INFO("========== Testing Concurrency ==========");
    std::thread t1(test_log_concurrency);
    std::thread t2(test_log_concurrency);

    t1.join();
    t2.join();

    LOG_INFO("========== Test Finished ==========");

    // 在 main 结束前显式关闭，此时 spdlog 肯定还活着
    MprpcApplication::GetInstance().Shutdown();
    spdlog::shutdown();

    return 0;
}
