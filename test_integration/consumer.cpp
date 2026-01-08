#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <cstdlib> // for atoi
#include <thread> // 提供 std::this_thread::sleep_for
#include <chrono> // 提供时间单位，如 std::chrono::seconds

#include <mprpc/mprpcapplication.h>
#include <mprpc/rpcclient.h> // 包含 MprpcChannel 和 RpcClientConfig
#include <mprpc/rpccontroller.h>
#include "user.pb.h"

// ==========================================================
// 辅助函数：打印测试结果
// ==========================================================
void PrintResult(const std::string& test_name, 
                 bool is_failed, 
                 const std::string& error_msg, 
                 bool success,
                 const std::string& extra_info = "") {
    if (is_failed) {
        std::cout << "[FAIL] " << test_name << " Rpc Failed: " << error_msg << std::endl;
    } else {
        if (success) {
            std::cout << "[PASS] " << test_name << " Rpc Success! " << extra_info << std::endl;
        } else {
            std::cout << "[FAIL] " << test_name << " Business Error: " << extra_info << std::endl;
        }
    }
}

// ==========================================================
// 场景 1: ZK 服务发现模式 (推荐)
// ==========================================================
void TestZkDiscoveryMode(MprpcChannel& channel) {
    std::cout << "\n>>> Testing Zookeeper Discovery Mode <<<" << std::endl;

    // 1. 创建 Stub
    fixbug::UserServiceRpc_Stub stub(&channel);

    // 2. 发起调用 (Login)
    fixbug::LoginRequest login_req;
    login_req.set_name("lzz");
    login_req.set_pwd("123456");
    fixbug::LoginResponse login_resp;
    
    MprpcController controller; 

    // 发起同步调用
    stub.Login(&controller, &login_req, &login_resp, nullptr);

    PrintResult("Login", 
                controller.Failed(), 
                controller.ErrorText(), 
                login_resp.result().errcode() == 0,
                login_resp.success() ? "Authorized" : "Unauthorized");

    // 4. 发起调用 (Register) - 测试连接池复用
    fixbug::RegisterRequest reg_req;
    reg_req.set_id(2000);
    reg_req.set_name("li si");
    reg_req.set_pwd("666666");
    fixbug::RegisterResponse reg_resp;
    
    controller.Reset();

    stub.Register(&controller, &reg_req, &reg_resp, nullptr);

    PrintResult("Register", 
                controller.Failed(), 
                controller.ErrorText(), 
                reg_resp.result().errcode() == 0,
                "Registered ID: " + std::to_string(reg_req.id()));
}

// ==========================================================
// 场景 2: 并发压力测试 (Concurrency Test)
// 验证连接池和多线程安全性
// ==========================================================
void TestConcurrency(MprpcChannel& channel) {
    std::cout << "\n>>> Testing Concurrency (20 threads) <<<" << std::endl;

    // 使用 ZK 模式的 Channel (内部有连接池)

    static fixbug::UserServiceRpc_Stub stub(&channel);

    std::vector<std::thread> threads;
    int thread_count = 20;

    for (int i = 0; i < thread_count; ++i) {
        threads.emplace_back([i]() {
            fixbug::LoginRequest req;
            req.set_name("thread_" + std::to_string(i));
            req.set_pwd("pwd");
            fixbug::LoginResponse resp;
            MprpcController cntl;

            // 模拟并发调用
            stub.Login(&cntl, &req, &resp, nullptr);

            if (cntl.Failed()) {
                std::cout << "[Thread " << i << "] Failed: " << cntl.ErrorText() << std::endl;
            } else {
                // std::cout << "[Thread " << i << "] Success" << std::endl;
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }
    std::cout << "[PASS] Concurrency Test Finished" << std::endl;
}

int main(int argc, char **argv) {
    // 1. 初始化框架
    MprpcApplication::Init(argc, argv);

    // 2. 准备客户端配置
    RpcClientConfig client_config;
    // 从配置文件或默认值设置
    client_config.rpc_timeout_ms = 5000;
    client_config.connect_timeout_ms = 2000;
    client_config.connection_pool_size = 4; // 每个目标 IP 开 4 个长连接

    MprpcChannel channel("", 0, client_config); // 会创建一个全局的 ZKClient 实例

    // 3. 执行测试用例
    TestZkDiscoveryMode(channel);   // ZK 服务发现模式测试

    LOG_INFO("Tests finished. Sleeping for 10 seconds before exit..."); // 暂停10秒，测试健壮性
    std::this_thread::sleep_for(std::chrono::seconds(10));

    TestConcurrency(channel);   // 并发压力测试

    // 4. 显式清理全局单例
    MprpcApplication::GetInstance().Shutdown();
    spdlog::shutdown();

    _exit(0);
}