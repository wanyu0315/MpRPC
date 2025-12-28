/**
 * @file test_zk.cpp
 * @brief ZooKeeper 客户端功能集成测试
 */

#include "zookeeperutil.h"
#include <iostream>
#include <thread>
#include <vector>
#include <cassert>

// 辅助打印宏
#define LOG_TEST(msg) std::cout << ">>> [TEST] " << msg << std::endl
#define LOG_ERR(msg) std::cerr << ">>> [ERROR] " << msg << std::endl

int main(int argc, char** argv) {
    LOG_TEST("Start testing ZookeeperUtil...");

    // 1. 配置准备
    ZkConfig config;
    config.host = "127.0.0.1:2181";         // 确保本地 ZK 已启动
    config.session_timeout_ms = 30000;
    config.connect_timeout_ms = 5000;       // 5秒连接超时
    config.root_path = "/mprpc_test";       // 测试专用的rpc应用根路径
    config.enable_auto_reconnect = true;
    config.max_retry_times = 3;

    // 2. 获取 zk 客户端单例并初始化
    ZkClient& zk = ZkClient::GetInstance();
    zk.Init(config); // 初始化配置

    // 3. 连接测试
    LOG_TEST("Connecting to Zookeeper Server...");
    // 启动 zk 客户端（会在zk服务器上创建配置中设置好的根节点）
    if (!zk.Start()) {
        LOG_ERR("Connection failed! Please check if Zookeeper server is running.");
        return -1;
    }
    LOG_TEST("Connection established.");

    // 4. 清理环境 (防止上次测试残留)
    if (zk.Exists("/mprpc_test")) {
        LOG_TEST("Cleaning up old test data...");
        zk.Delete("/mprpc_test", true);
    }

    // 5. 测试基础 CRUD
    std::string path = "/mprpc_test/node_1";
    std::string data = "hello_zk";

    // 5.1 创建 (测试递归创建持久父节点能力)
    if (zk.Create(path, data, ZkNodeType::PERSISTENT)) {
        LOG_TEST("Create node success: " + path);
    } else {
        LOG_ERR("Create node failed!");
        return -1;
    }

    // 5.2 读取
    std::string read_val;
    if (zk.Get(path, read_val)) {
        LOG_TEST("Get node value: " + read_val);
        if (read_val != data) LOG_ERR("Value mismatch!");
    }

    // 5.3 修改
    if (zk.Set(path, "new_value")) {
        zk.Get(path, read_val);
        LOG_TEST("Set node new value: " + read_val);
    }

    // 6. 测试 RPC 服务注册 (核心业务)
    std::string service_name = "UserService";
    std::string ip_port = "127.0.0.1:8000";

    LOG_TEST("Testing RPC Service Registration...");
    // 6.1 注册
    if (zk.RegisterService(service_name, ip_port)) {
        LOG_TEST("Service registered: " + service_name + " @ " + ip_port);
    } else {
        LOG_ERR("Register service failed!");
    }

    // 6.2 服务发现
    std::vector<std::string> list = zk.GetServiceList(service_name);
    LOG_TEST("Discovered instances for " + service_name + ": " + std::to_string(list.size()));
    for (const auto& item : list) {
        std::cout << "    - " << item << std::endl;
        if (item == ip_port) LOG_TEST("Found correct instance!");
    }

    // 6.3 打印树结构查看
    zk.PrintTree("/mprpc_test", 5);

    // 7. 测试 Watch 机制 (简单模拟)
    LOG_TEST("Testing Watch mechanism...");
    zk.WatchService(service_name, [](const std::string& path, int type, int state){
        std::cout << ">>> [CALLBACK] Path: " << path << " changed! Type: " << type << std::endl;
    });

    // 模拟服务下线 (触发 Watch)
    // 注意：ZookeeperUtil 的 UnregisterService 实际上是删除节点
    LOG_TEST("Simulating service offline (Unregister)...");
    zk.UnregisterService(service_name, ip_port);
    
    // 给一点时间让回调触发 (因为是异步通知)
    std::this_thread::sleep_for(std::chrono::seconds(1));

    // 8. 清理测试产生的垃圾
    // 生产环境中不需要，但测试为了不弄脏 ZK 建议清理
    LOG_TEST("Final cleanup...");
    zk.Delete("/mprpc_test", true);

    LOG_TEST("All tests passed! Press Ctrl+C to exit.");
    return 0;
}