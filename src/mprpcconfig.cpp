#include "mprpcconfig.h"
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm> // for std::string操作

// 单例模式获取实例
MprpcConfig& MprpcConfig::GetInstance() {
    static MprpcConfig instance;
    return instance;
}

// 核心功能：解析加载配置文件
void MprpcConfig::LoadConfigFile(const char *config_file) {
    // 使用 std::ifstream 标准流打开文件，比 C 风格 FILE* 更安全
    std::ifstream file(config_file);
    if (!file.is_open()) {
        std::cerr << "[MprpcConfig] Fatal Error: Config file not found: " << config_file << std::endl;
        // 配置文件是启动必须项，如果加载失败直接退出
        exit(EXIT_FAILURE);
    }

    std::string line;
    std::string current_section; // 记录当前的段落，如 [server]

    // 逐行读取
    while (std::getline(file, line)) {
        // 1. 预处理：去掉整行前后的空格
        Trim(line);

        // 2. 跳过空行和注释行 (支持 # 和 ; 作为注释)
        if (line.empty() || line[0] == '#' || line[0] == ';') {
            continue;
        }

        // 3. 解析 Section (例如: [RpcServer])
        if (line.front() == '[' && line.back() == ']') {
            // 提取中间的名称: [RpcServer] -> RpcServer
            current_section = line.substr(1, line.size() - 2);
            Trim(current_section);
            continue; //这一行处理完了，读下一行
        }

        // 4. 解析 Key=Value
        size_t idx = line.find('=');
        if (idx == std::string::npos) {
            // 没有找到=号，说明格式不对，忽略
            continue;
        }

        std::string key = line.substr(0, idx);
        std::string value = line.substr(idx + 1);
        
        // 清理 Key 和 Value 旁边的空格 (例如: " ip = 127.0.0.1 " -> "ip", "127.0.0.1")
        Trim(key);
        Trim(value);

        // 5. 组合最终的 Key
        // 如果在 section 下，key 变成 "section.key"
        std::string final_key;
        if (!current_section.empty()) {
            final_key = current_section + "." + key;
        } else {
            final_key = key;
        }

        // 6. 存入 Map (加锁保护，虽然主要是启动时单线程加载，但为了习惯加锁)
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_configMap[final_key] = value;
        }

        // 调试日志：打印加载到的配置
        // std::cout << "[Config] Loaded: " << final_key << " = " << value << std::endl;
    }

    file.close();
}

// 查询配置项信息 (String)
std::string MprpcConfig::Load(const std::string &key) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_configMap.find(key);
    if (it == m_configMap.end()) {
        return ""; // 没找到返回空串
    }
    return it->second;
}

// 查询配置项信息 (Int) - 这是一个非常实用的辅助函数
int MprpcConfig::LoadInt(const std::string &key, int default_value) const {
    std::string val_str = Load(key);
    if (val_str.empty()) {
        return default_value;
    }
    try {
        return std::stoi(val_str);
    } catch (...) {
        // 如果转换失败（比如配置写了 "abc"），返回默认值
        return default_value;
    }
}

// 去掉字符串前后的空格 (Util)
void MprpcConfig::Trim(std::string &src_buf) {
    // 找到第一个不是空格\t\r\n的字符位置
    if (src_buf.empty()) return;

    // 去掉前导空格
    size_t first = src_buf.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        // 全是空格
        src_buf = "";
        return;
    }

    // 去掉后置空格
    size_t last = src_buf.find_last_not_of(" \t\r\n");
    
    // 截取中间有效部分
    src_buf = src_buf.substr(first, (last - first + 1));
}

// 加载环境变量
void MprpcConfig::LoadEnvVariables(const std::string &prefix) { // prefix用于限定可被加载的环境变量范围
    // 声明外部全局变量，指向环境变量表
    extern char **environ;

    std::lock_guard<std::mutex> lock(m_mutex); // 保护 m_configMap

    // 遍历所有环境变量
    for (char **env = environ; *env != nullptr; ++env) {
        std::string env_str = *env;
        
        // 查找等号位置
        size_t pos = env_str.find('=');
        if (pos == std::string::npos) continue;

        std::string env_key = env_str.substr(0, pos);
        std::string env_value = env_str.substr(pos + 1);

        // 1. 检查前缀 (例如只处理 RPC_ 开头的变量)
        if (env_key.size() > prefix.size() && env_key.substr(0, prefix.size()) == prefix) {
            
            // 2. 键名转换逻辑
            // 原始: RPC_RPCSERVER_PORT
            // 去前缀: RPCSERVER_PORT
            std::string config_key = env_key.substr(prefix.size());
            
            // 转小写: rpcserver_port
            std::transform(config_key.begin(), config_key.end(), config_key.begin(), ::tolower);
            
            // 下划线变点: rpcserver.port
            std::replace(config_key.begin(), config_key.end(), '_', '.');

            // 3. 存入/覆盖 Map
            m_configMap[config_key] = env_value;

            // 调试日志
            // std::cout << "[Config] Env Loaded: " << env_key << " -> " << config_key << " = " << env_value << std::endl;
        }
    }
}