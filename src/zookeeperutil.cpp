/**
 * @file zookeeperutil.cpp
 * @brief ZooKeeper 客户端工具类实现
 */

#include "zookeeperutil.h"

#include <unistd.h>
#include <algorithm>
#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

// ============================================================================
// 构造和析构
// ============================================================================

/**
 * @brief 私有构造函数（单例模式）
 * 并没有在这里建立连接，因为构造函数无法返回值（无法报错），所以真正的初始化逻辑放在了 `Init` 和 `Start` 中。
 */
ZkClient::ZkClient() 
    : zk_handle_(nullptr),
      is_connected_(false),
      connection_state_(0) {
  // 构造函数留空，真正的初始化在 Init() 中完成
}

/**
 * @brief 析构函数
 * @details RAII思想：自动关闭连接，释放资源
 */
ZkClient::~ZkClient() {
  Stop();
}

// ============================================================================
// 单例访问
// ============================================================================

/**
 * @brief 获取全局单例实例（线程安全）
 */
ZkClient& ZkClient::GetInstance() {
  static ZkClient instance;
  return instance;
}

// ============================================================================
// 初始化与连接
// ============================================================================

/**
 * @brief 初始化配置
 * @param config 简单的参数拷贝，保存 IP、端口、超时时间等，为连接做准备。
 */
void ZkClient::Init(const ZkConfig& config) {
  config_ = config;
  
  std::cout << "[ZkClient] Initialized with config:" << std::endl;
  std::cout << "  Host: " << config_.host << std::endl;
  std::cout << "  Session Timeout: " << config_.session_timeout_ms << "ms" << std::endl;
  std::cout << "  Root Path: " << config_.root_path << std::endl;
}

/**
 * @brief 启动 ZooKeeper 客户端，连接服务器
 * @return true 连接成功, false 连接失败
 * @details
 * 执行步骤：
 * 1. 检查配置是否有效
 * 2. 调用 zookeeper_init() 初始化客户端
 * 3. 使用条件变量等待连接成功（带超时）
 * 4. 连接失败时根据配置自动重试
 */
bool ZkClient::Start() {
  // 1. 参数校验
  if (config_.host.empty()) {
    std::cerr << "[ZkClient] Error: Host is empty! Call Init() first." << std::endl;
    return false;
  }

  if (zk_handle_ != nullptr) {
    std::cerr << "[ZkClient] Warning: Already connected!" << std::endl;
    return true;
  }

  std::cout << "[ZkClient] Connecting to ZooKeeper: " << config_.host << std::endl;

  // 2. 初始化 ZooKeeper 客户端
  // 调用原生 C API
  // 注意：zookeeper_init 是异步的，立即返回，真正的连接在后台线程中完成
  zk_handle_ = zookeeper_init(
      config_.host.c_str(),           // 服务器地址列表
      GlobalWatcher,                  // 全局 Watcher 回调
      config_.session_timeout_ms,     // 会话超时时间
      nullptr,                        // clientid（nullptr 表示新会话）
      this,                           // 传递给 Watcher 的上下文指针
      0                               // flags
  );

  if (zk_handle_ == nullptr) {
    std::cerr << "[ZkClient] Error: zookeeper_init failed!" << std::endl;
    return false;
  }

  // 3. 等待连接成功（使用条件变量 + 超时）
  std::unique_lock<std::mutex> lock(connect_mutex_);
  bool connected = connect_cv_.wait_for( // 当前线程通过条件变量进入休眠等待，直到 GlobalWatcher 收到连接成功的通知
      lock, 
      std::chrono::milliseconds(config_.connect_timeout_ms),
      [this]() { return is_connected_.load(); }
  );

  if (connected) {
    std::cout << "[ZkClient] Connected successfully!" << std::endl;
    
    // 4. 创建rpc服务根路径（如果不存在）
    if (!config_.root_path.empty() && config_.root_path != "/") {
      CreateParentNodes(config_.root_path);
    }
    return true;
  } else {
    std::cerr << "[ZkClient] Connection timeout!" << std::endl;
    
    // 5. 连接失败，尝试重试
    if (config_.enable_auto_reconnect) {
      std::cerr << "[ZkClient] Retrying connection..." << std::endl;
      return Reconnect();
    }
    
    return false;
  }
}

/**
 * @brief 停止 ZooKeeper 客户端，关闭连接
 */
void ZkClient::Stop() {
  if (zk_handle_ != nullptr) {
    std::cout << "[ZkClient] Closing connection..." << std::endl;
    
    // 关闭连接（这是一个阻塞操作）
    int ret = zookeeper_close(zk_handle_);
    if (ret != ZOK) {
      std::cerr << "[ZkClient] Warning: zookeeper_close returned " 
                << ErrorToString(ret) << std::endl;
    }
    
    // 重置状态标志位
    zk_handle_ = nullptr;
    is_connected_ = false;
    connection_state_ = 0;
    
    std::cout << "[ZkClient] Connection closed." << std::endl;
  }
}

/**
 * @brief 检查是否已连接
 */
bool ZkClient::IsConnected() const {
  return is_connected_.load() && zk_handle_ != nullptr;
}

/**
 * @brief 获取连接状态字符串
 */
std::string ZkClient::GetStateString() const {
  if (!zk_handle_) return "UNINITIALIZED";
  
  int state = zoo_state(zk_handle_);
  switch (state) {
    case ZOO_EXPIRED_SESSION_STATE: return "EXPIRED";
    case ZOO_AUTH_FAILED_STATE:     return "AUTH_FAILED";
    case ZOO_CONNECTING_STATE:      return "CONNECTING";
    case ZOO_ASSOCIATING_STATE:     return "ASSOCIATING";
    case ZOO_CONNECTED_STATE:       return "CONNECTED";
    default:                        return "UNKNOWN";
  }
}

// ============================================================================
// 节点基本操作（CRUD）
// ============================================================================

/**
 * @brief 创建 ZooKeeper 节点
 * @param path 节点路径
 * @param data 节点数据
 * @param node_type 节点类型
 * @return true 创建成功, false 创建失败
 */
bool ZkClient::Create(const std::string& path, 
                     const std::string& data, 
                     ZkNodeType node_type) {
  if (!IsConnected()) {
    std::cerr << "[ZkClient] Error: Not connected!" << std::endl;
    return false;
  }

  total_operations_++;

  // 1. 检查节点是否已存在
  if (Exists(path)) {
    std::cout << "[ZkClient] Node already exists: " << path << std::endl;
    return true;  // 幂等操作
  }

  // 2. 创建父节点（如果不存在）
  // ZooKeeper 的原生 API 不支持递归创建路径，必须先手动创建父节点。
  if (!CreateParentNodes(path)) {
    std::cerr << "[ZkClient] Failed to create parent nodes for: " << path << std::endl;
    failed_operations_++;
    return false;
  }

  // 3. 创建节点
  char path_buffer[512];
  int flags = static_cast<int>(node_type);
  
  int ret = zoo_create(
      zk_handle_,
      path.c_str(),
      data.c_str(),
      data.size(),
      &ZOO_OPEN_ACL_UNSAFE,  // 权限：完全开放（生产环境应使用更安全的 ACL）
      flags,
      path_buffer,
      sizeof(path_buffer)
  );

  if (ret == ZOK) {
    std::cout << "[ZkClient] Node created: " << path << std::endl;
    return true;
  } else {
    std::cerr << "[ZkClient] Failed to create node: " << path 
              << ", error: " << ErrorToString(ret) << std::endl;
    failed_operations_++;
    return false;
  }
}

/**
 * @brief 获取节点数据
 * @param path 节点路径
 * @param data [输出] 节点数据
 * @param watch 是否设置 Watch
 * @return true 成功, false 失败
 */
bool ZkClient::Get(const std::string& path, std::string& data, bool watch) {
  if (!IsConnected()) {
    std::cerr << "[ZkClient] Error: Not connected!" << std::endl;
    return false;
  }

  total_operations_++;

  char buffer[10240];  // 10KB 缓冲区（根据实际需求调整）
  int buffer_len = sizeof(buffer);

  int ret = zoo_get(
      zk_handle_,
      path.c_str(),
      watch ? 1 : 0,  // 是否设置 Watch
      buffer,
      &buffer_len,
      nullptr         // stat 信息（版本、修改时间等）
  );

  if (ret == ZOK) {
    data.assign(buffer, buffer_len);
    std::cout << "[ZkClient] Got data from node: " << path 
              << " (" << buffer_len << " bytes)" << std::endl;
    return true;
  } else {
    std::cerr << "[ZkClient] Failed to get node: " << path 
              << ", error: " << ErrorToString(ret) << std::endl;
    failed_operations_++;
    return false;
  }
}

/**
 * @brief 修改指定路径 path 对应节点（znode）的数据内容
 * @param path 节点路径
 * @param data 新数据
 * @return true 成功, false 失败
 */
bool ZkClient::Set(const std::string& path, const std::string& data) {
  if (!IsConnected()) {
    std::cerr << "[ZkClient] Error: Not connected!" << std::endl;
    return false;
  }

  total_operations_++;

  int ret = zoo_set(
      zk_handle_,
      path.c_str(),
      data.c_str(),
      data.size(),
      -1  // version（-1 表示不检查版本，强制更新）
  );

  if (ret == ZOK) {
    std::cout << "[ZkClient] Set data for node: " << path << std::endl;
    return true;
  } else {
    std::cerr << "[ZkClient] Failed to set node: " << path 
              << ", error: " << ErrorToString(ret) << std::endl;
    failed_operations_++;
    return false;
  }
}

/**
 * @brief 删除指定路径对应的 znode
 * @param path 节点路径
 * @param recursive 是否递归删除子节点
 * @return true 成功, false 失败
 */
bool ZkClient::Delete(const std::string& path, bool recursive) {
  if (!IsConnected()) {
    std::cerr << "[ZkClient] Error: Not connected!" << std::endl;
    return false;
  }

  total_operations_++;

  // 如果需要递归删除
  if (recursive) {
    return DeleteRecursive(path);
  }

  // 普通删除（不能有子节点）
  int ret = zoo_delete(zk_handle_, path.c_str(), -1);

  if (ret == ZOK) {
    std::cout << "[ZkClient] Deleted node: " << path << std::endl;
    return true;
  } else if (ret == ZNONODE) {
    std::cout << "[ZkClient] Node not exists: " << path << std::endl;
    return true;  // 幂等操作
  } else {
    std::cerr << "[ZkClient] Failed to delete node: " << path 
              << ", error: " << ErrorToString(ret) << std::endl;
    failed_operations_++;
    return false;
  }
}

/**
 * @brief 检查节点是否存在
 * @param path 节点路径
 * @return true 存在, false 不存在
 */
bool ZkClient::Exists(const std::string& path) {
  if (!IsConnected()) {
    return false;
  }

  int ret = zoo_exists(zk_handle_, path.c_str(), 0, nullptr);
  return ret == ZOK;
}

/**
 * @brief 获取子节点列表，如果watch = 1 就同时挂载监听
 * @param path 父节点路径
 * @param children [输出] 子节点名称列表
 * @param watch 是否设置 Watch
 * @return true 成功, false 失败
 */
bool ZkClient::GetChildren(const std::string& path, 
                          std::vector<std::string>& children, 
                          bool watch) {
  if (!IsConnected()) {
    std::cerr << "[ZkClient] Error: Not connected!" << std::endl;
    return false;
  }

  total_operations_++;

  struct String_vector str_vec;
  int ret = zoo_get_children(
      zk_handle_,
      path.c_str(),
      watch ? 1 : 0,
      &str_vec
  );

  if (ret == ZOK) {
    children.clear();
    for (int i = 0; i < str_vec.count; ++i) {
      children.push_back(str_vec.data[i]);
    }
    
    // 释放 ZooKeeper 分配的内存
    deallocate_String_vector(&str_vec);
    
    std::cout << "[ZkClient] Got " << children.size() 
              << " children for node: " << path << std::endl;
    return true;
  } else {
    std::cerr << "[ZkClient] Failed to get children: " << path 
              << ", error: " << ErrorToString(ret) << std::endl;
    failed_operations_++;
    return false;
  }
}

// ============================================================================
// RPC 服务注册与发现
// ============================================================================

/**
 * @brief 注册 RPC 服务到 ZooKeeper
 * @param service_name 服务名称
 * @param service_addr 服务地址
 * @return true 成功, false 失败
 * @details
 * 注册路径：/rpc/{service_name}/{service_addr}
 * 节点类型：临时节点（服务下线自动删除）
 */
bool ZkClient::RegisterService(const std::string& service_name, 
                              const std::string& service_addr) {
  // 1. 构造服务路径（也就是即将生成的Znode节点）
  std::string service_path = config_.root_path + "/" + service_name; // "rpc/UserService"
  std::string instance_path = service_path + "/" + service_addr;  // "rpc/UserService/<service_addr>"

  std::cout << "[ZkClient] Registering service: " << instance_path << std::endl;

  // 2. 创建服务名称节点（持久节点：PERSISTENT）
  if (!Exists(service_path)) {
    if (!Create(service_path, "", ZkNodeType::PERSISTENT)) {
      std::cerr << "[ZkClient] Failed to create service node: " << service_path << std::endl;
      return false;
    }
  }

  // 3. 创建服务实例节点（临时节点：EPHEMERAL）
  // 临时节点会在客户端断开连接时自动删除
  if (!Create(instance_path, service_addr, ZkNodeType::EPHEMERAL)) {
    std::cerr << "[ZkClient] Failed to create instance node: " << instance_path << std::endl;
    return false;
  }

  std::cout << "[ZkClient] Service registered successfully: " << instance_path << std::endl;
  return true;
}

/**
 * @brief 注销 RPC 服务
 * @param service_name 服务名称
 * @param service_addr 服务地址
 * @return true 成功, false 失败
 */
bool ZkClient::UnregisterService(const std::string& service_name, 
                                const std::string& service_addr) {
  std::string instance_path = config_.root_path + "/" + service_name + "/" + service_addr;
  
  std::cout << "[ZkClient] Unregistering service: " << instance_path << std::endl;
  
  return Delete(instance_path, false); // 只删除临时节点即可
}

/**
 * @brief 获取指定服务的所有实例列表
 * @param service_name 服务名称
 * @return 服务地址列表
 */
std::vector<std::string> ZkClient::GetServiceList(const std::string& service_name) {
  std::string service_path = config_.root_path + "/" + service_name;
  
  // server_path = /rpc/UserService，下面的子节点都是服务实例
  std::vector<std::string> children;
  if (GetChildren(service_path, children, false)) {
    std::cout << "[ZkClient] Found " << children.size() 
              << " instances for service: " << service_name << std::endl;
    return children;
  }
  
  std::cerr << "[ZkClient] Failed to get service list: " << service_name << std::endl;
  return {};
}

/**
 * @brief 监听服务变化
 * @param service_name 服务名称
 * @param callback 回调函数
 * @return true 成功, false 失败
 */
bool ZkClient::WatchService(const std::string& service_name, ZkWatchCallback callback) {
  std::string service_path = config_.root_path + "/" + service_name;
  
  // 注册 Watch 回调
  SetWatcher(service_path, callback);
  
  // 获取子节点列表并设置 Watch
  std::vector<std::string> children;
  return GetChildren(service_path, children, true);
}

// ============================================================================
// Watch 监听机制
// ============================================================================

/**
 * @brief 绑定某个节点path与对应的 Watch 回调函数
 */
void ZkClient::SetWatcher(const std::string& path, ZkWatchCallback callback) {
  std::lock_guard<std::mutex> lock(watchers_mutex_);
  watchers_[path] = callback;
  std::cout << "[ZkClient] Watch registered for path: " << path << std::endl;
}

/**
 * @brief 清除 Watch 回调
 */
void ZkClient::ClearWatcher(const std::string& path) {
  std::lock_guard<std::mutex> lock(watchers_mutex_);
  watchers_.erase(path);
  std::cout << "[ZkClient] Watch cleared for path: " << path << std::endl;
}

// ============================================================================
// 辅助功能
// ============================================================================

/**
 * @brief 打印 ZooKeeper 树形结构
 */
void ZkClient::PrintTree(const std::string& root_path, int max_depth) {
  if (!IsConnected()) {
    std::cerr << "[ZkClient] Error: Not connected!" << std::endl;
    return;
  }

  std::cout << "\n========== ZooKeeper Tree ===========" << std::endl;
  PrintTreeRecursive(root_path, 0, max_depth, "");
  std::cout << "=====================================\n" << std::endl;
}

// ============================================================================
// 内部方法
// ============================================================================

/**
 * @brief 全局 Watcher 回调（静态函数）
 * @details ZooKeeper C API 要求 Watcher 是静态函数或全局函数
 */
void ZkClient::GlobalWatcher(zhandle_t* zh, int type, int state, 
                            const char* path, void* context) {
  // 通过 context 指针获取 ZkClient 实例
  ZkClient* client = static_cast<ZkClient*>(context);
  if (client) {
    client->OnWatcherEvent(type, state, path ? path : "");
  }
}

/**
 * @brief 处理 Watcher 事件
 * @param type 事件类型
 * @param state 连接状态
 * @param path 节点路径
 */
void ZkClient::OnWatcherEvent(int type, int state, const std::string& path) {
  std::cout << "[ZkClient] Watcher event: type=" << type 
            << ", state=" << state 
            << ", path=" << path << std::endl;

  // 1. 处理会话事件
  if (type == ZOO_SESSION_EVENT) {
    // 如果是连接成功事件
    if (state == ZOO_CONNECTED_STATE) {
      std::cout << "[ZkClient] Connected to ZooKeeper!" << std::endl;
      
      // 更新连接状态
      is_connected_ = true;
      connection_state_ = state;
      
      // 通知等待的线程
      connect_cv_.notify_all();
      
    } else if (state == ZOO_EXPIRED_SESSION_STATE) {
      std::cerr << "[ZkClient] Session expired! Reconnecting..." << std::endl;
      
      is_connected_ = false;
      connection_state_ = state;
      
      // 自动重连
      if (config_.enable_auto_reconnect) {
        Reconnect();
      }
      
    } else if (state == ZOO_CONNECTING_STATE) {
      std::cout << "[ZkClient] Reconnecting..." << std::endl;
      is_connected_ = false;
      connection_state_ = state;
    }
  }

  // 2. 触发用户注册的 Watch 回调
  {
    std::lock_guard<std::mutex> lock(watchers_mutex_);
    auto it = watchers_.find(path);
    if (it != watchers_.end()) {
      try {
        it->second(path, type, state);
      } catch (const std::exception& e) {
        std::cerr << "[ZkClient] Exception in Watch callback: " << e.what() << std::endl;
      }
    }
  }
}

/**
 * @brief 重连机制
 */
bool ZkClient::Reconnect() {
  for (int i = 0; i < config_.max_retry_times; ++i) {
    std::cout << "[ZkClient] Reconnect attempt " << (i + 1) 
              << "/" << config_.max_retry_times << std::endl;
    
    Stop();  // 先关闭旧连接
    
    std::this_thread::sleep_for(std::chrono::seconds(1));  // 等待 1 秒
    
    if (Start()) {
      std::cout << "[ZkClient] Reconnected successfully!" << std::endl;
      return true;
    }
  }
  
  std::cerr << "[ZkClient] Reconnection failed after " 
            << config_.max_retry_times << " attempts!" << std::endl;
  return false;
}

/**
 * @brief 递归创建父节点
 */
bool ZkClient::CreateParentNodes(const std::string& path) {
  if (path.empty() || path == "/") {
    return true;
  }

  // 查找最后一个 '/' 的位置
  size_t last_slash = path.find_last_of('/');
  if (last_slash == 0) {
    return true;  // 父节点是根节点
  }

  std::string parent_path = path.substr(0, last_slash);
  
  // 如果父节点已存在，直接返回
  if (Exists(parent_path)) {
    return true;
  }

  // 递归创建祖先节点
  if (!CreateParentNodes(parent_path)) {
    return false;
  }

  // 创建父节点（持久节点）
  return Create(parent_path, "", ZkNodeType::PERSISTENT);
}

/**
 * @brief 递归删除节点
 * 1. 获取 path 的所有子节点
 * 2. 递归删除每一个子节点
 * 3. 删除 path 自身
 */
bool ZkClient::DeleteRecursive(const std::string& path) {
  // 1. 获取所有子节点
  std::vector<std::string> children;
  if (!GetChildren(path, children, false)) {
    return false;
  }

  // 2. 递归删除所有子节点
  for (const auto& child : children) {
    std::string child_path = path + "/" + child;
    if (!DeleteRecursive(child_path)) {
      return false;
    }
  }

  // 3. 删除当前节点
  return Delete(path, false);
}

/**
 * @brief 递归打印树形结构
 */
void ZkClient::PrintTreeRecursive(const std::string& path, int depth, int max_depth, 
                                 const std::string& prefix) {
  if (max_depth >= 0 && depth > max_depth) {
    return;
  }

  // 打印当前节点
  std::cout << prefix << "├─ " << path << std::endl;

  // 获取子节点
  std::vector<std::string> children;
  if (!GetChildren(path, children, false)) {
    return;
  }

  // 递归打印子节点
  for (size_t i = 0; i < children.size(); ++i) {
    std::string child_path = (path == "/" ? "/" : path + "/") + children[i];
    std::string new_prefix = prefix + (i == children.size() - 1 ? "   " : "│  ");
    PrintTreeRecursive(child_path, depth + 1, max_depth, new_prefix);
  }
}

/**
 * @brief 将错误码转换为字符串
 */
std::string ZkClient::ErrorToString(int error_code) const {
  switch (error_code) {
    case ZOK:                   return "ZOK";
    case ZSYSTEMERROR:          return "ZSYSTEMERROR";
    case ZRUNTIMEINCONSISTENCY: return "ZRUNTIMEINCONSISTENCY";
    case ZDATAINCONSISTENCY:    return "ZDATAINCONSISTENCY";
    case ZCONNECTIONLOSS:       return "ZCONNECTIONLOSS";
    case ZMARSHALLINGERROR:     return "ZMARSHALLINGERROR";
    case ZUNIMPLEMENTED:        return "ZUNIMPLEMENTED";
    case ZOPERATIONTIMEOUT:     return "ZOPERATIONTIMEOUT";
    case ZBADARGUMENTS:         return "ZBADARGUMENTS";
    case ZINVALIDSTATE:         return "ZINVALIDSTATE";
    case ZNONODE:               return "ZNONODE";
    case ZNOAUTH:               return "ZNOAUTH";
    case ZBADVERSION:           return "ZBADVERSION";
    case ZNOCHILDRENFOREPHEMERALS: return "ZNOCHILDRENFOREPHEMERALS";
    case ZNODEEXISTS:           return "ZNODEEXISTS";
    case ZNOTEMPTY:             return "ZNOTEMPTY";
    case ZSESSIONEXPIRED:       return "ZSESSIONEXPIRED";
    case ZINVALIDCALLBACK:      return "ZINVALIDCALLBACK";
    case ZINVALIDACL:           return "ZINVALIDACL";
    case ZAUTHFAILED:           return "ZAUTHFAILED";
    case ZCLOSING:              return "ZCLOSING";
    case ZNOTHING:              return "ZNOTHING";
    case ZSESSIONMOVED:         return "ZSESSIONMOVED";
    default:                    return "UNKNOWN_ERROR(" + std::to_string(error_code) + ")";
  }
}