#pragma once

#include <string>
#include <unordered_map>
#include <mutex>

// 框架核心配置类
// 负责加载配置文件，并提供线程安全的查询接口
class MprpcConfig {
public:
    // 获取单例对象的引用
    static MprpcConfig& GetInstance();

    // 加载解析配置文件
    // 参数: config_file 配置文件路径
    void LoadConfigFile(const char *config_file);

    // 加载环境变量 (进阶：用于 Docker/K8s)
    // prefix: 环境变量的前缀，例如 "RPC_"
    // 作用: 将 RPC_SERVER_IP 转换为 server.ip 并存入 map
    void LoadEnvVariables(const std::string &prefix);

    // 查询配置项 - 返回字符串
    // key: 配置项名称 (如果是section下的，格式为 "section.key")
    std::string Load(const std::string &key) const;

    // 查询配置项 - 返回整数 (辅助函数，方便业务层直接使用)
    int LoadInt(const std::string &key, int default_value = 0) const;

private:
    MprpcConfig() = default;
    MprpcConfig(const MprpcConfig&) = delete;
    MprpcConfig& operator=(const MprpcConfig&) = delete;

    // 核心数据结构：存储配置项的键值对
    // key: "rpcserver.ip"  value: "127.0.0.1"
    std::unordered_map<std::string, std::string> m_configMap;
    
    // 读写锁：虽然配置加载后通常只读，但为了严谨的线程安全，保留互斥锁
    mutable std::mutex m_mutex;

    // 内部工具：去掉字符串前后的空格
    void Trim(std::string &src_buf);
};