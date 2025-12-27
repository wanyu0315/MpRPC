#include "mprpcapplication.h"
#include <iostream>

int main(int argc, char **argv) {
    // 1. 初始化框架
    std::cout << "--- 1. Begin Init ---" << std::endl;
    MprpcApplication::Init(argc, argv);

    // 2. 获取配置单例
    MprpcConfig& config = MprpcApplication::GetInstance().GetConfig();

    // 3. 打印配置信息，验证是否加载成功
    std::cout << "\n--- 2. Verify Configuration ---" << std::endl;
    
    // 读取我们在配置文件里写的值
    std::cout << "rpcserver.ip:   " << config.Load("rpcserver.ip") << std::endl;
    std::cout << "rpcserver.port: " << config.Load("rpcserver.port") << std::endl;
    std::cout << "zookeeper.ip:   " << config.Load("zookeeper.ip") << std::endl;

    // 4. 验证单例是否有效
    std::cout << "\n--- 3. Verify Singleton ---" << std::endl;
    MprpcConfig& config2 = MprpcApplication::GetInstance().GetConfig();
    if (&config == &config2) {
        std::cout << "Singleton works! Addr: " << &config << std::endl;
    } else {
        std::cerr << "Singleton Error!" << std::endl;
    }

    return 0;
}