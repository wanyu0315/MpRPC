/**
 * @file zookeeperutil.h
 * @brief ZooKeeper 客户端工具类（准工业级封装）
 * @details
 * 核心功能：
 * 1. 连接管理：自动重连、连接池
 * 2. 节点操作：创建、读取、更新、删除（CRUD）
 * 3. 监听机制：Watch 回调注册
 * 4. 服务注册/发现：RPC 服务的注册与查询
 * 5. 错误处理：重试、超时、日志
 * 
 * 设计模式：
 * - 单例模式：全局唯一 ZooKeeper 客户端
 * - 观察者模式：Watch 回调机制
 * - RAII：自动资源管理
 * 
 * 线程安全：是（内部使用互斥锁）
 */

#pragma once

#include <zookeeper/zookeeper.h>
#include <atomic>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

// ============================================================================
// ZooKeeper 配置参数
// ============================================================================
/**
 * @brief ZooKeeper 客户端配置
 */
struct ZkConfig {
  std::string host;                   // ZooKeeper 服务器地址（如 "127.0.0.1:2181"）
  int session_timeout_ms = 6000;      // 会话超时时间（毫秒）
  int connect_timeout_ms = 3000;      // 连接超时时间（毫秒）
  int max_retry_times = 3;            // 最大重试次数
  bool enable_auto_reconnect = true;  // 是否启用自动重连
  std::string root_path = "/rpc";     // RPC 服务的根路径
};

// ============================================================================
// ZooKeeper 节点类型
// ============================================================================
/**
 * @brief ZooKeeper 节点类型枚举
 */
enum class ZkNodeType {
  PERSISTENT,           // 持久节点（默认）
  EPHEMERAL, // 临时节点（会话结束自动删除）
  SEQUENCE  // 顺序节点（节点名后自动追加序号）
};

// ============================================================================
// Watch 回调类型
// ============================================================================
/**
 * @brief Watch 事件回调函数类型
 * @param path 触发事件的节点路径
 * @param type 事件类型（ZOO_CREATED_EVENT/ZOO_DELETED_EVENT/ZOO_CHANGED_EVENT）
 * @param state 连接状态（ZOO_CONNECTED_STATE/ZOO_EXPIRED_SESSION_STATE）
 */
using ZkWatchCallback = std::function<void(const std::string& path, int type, int state)>;

// ============================================================================
// ZooKeeper 客户端类
// ============================================================================
/**
 * @brief ZooKeeper 客户端工具类（单例）
 * @details
 * 核心功能：
 * 1. 连接管理：Start() / Stop() / IsConnected()
 * 2. 节点操作：Create() / Get() / Set() / Delete() / Exists()
 * 3. 服务注册：RegisterService() / UnregisterService()
 * 4. 服务发现：GetServiceList() / WatchService()
 * 5. 监听机制：SetWatcher() / ClearWatcher()
 * 
 * 使用示例：
 * ```cpp
 * // 1. 初始化并连接
 * ZkClient& zk = ZkClient::GetInstance();
 * ZkConfig config;
 * config.host = "127.0.0.1:2181";
 * zk.Init(config);
 * zk.Start();
 * 
 * // 2. 注册 RPC 服务
 * zk.RegisterService("UserService", "192.168.1.100:8080");
 * 
 * // 3. 获取服务列表
 * auto services = zk.GetServiceList("UserService");
 * 
 * // 4. 监听服务变化
 * zk.WatchService("UserService", [](const std::string& path, int type, int state) {
 *   std::cout << "Service changed: " << path << std::endl;
 * });
 * ```
 */
class ZkClient {
 public:
  // ========== 单例访问 ==========
  
  /**
   * @brief 获取全局单例实例
   * @return ZkClient 单例引用
   */
  static ZkClient& GetInstance();

  // ========== 初始化与连接 ==========
  
  /**
   * @brief 初始化 ZooKeeper 客户端配置
   * @param config 配置参数
   */
  void Init(const ZkConfig& config);

  /**
   * @brief 启动 ZooKeeper 客户端，连接服务器
   * @return true 连接成功, false 连接失败
   * @details
   * 执行流程：
   * 1. 调用 zookeeper_init() 初始化客户端
   * 2. 等待连接成功（使用条件变量）
   * 3. 连接失败时根据配置自动重试
   */
  bool Start();

  /**
   * @brief 停止 ZooKeeper 客户端，关闭连接
   */
  void Stop();

  /**
   * @brief 检查是否已连接到 ZooKeeper 服务器
   * @return true 已连接, false 未连接
   */
  bool IsConnected() const;

  /**
   * @brief 获取连接状态字符串（用于日志）
   * @return 状态描述（"CONNECTED" / "DISCONNECTED" / "EXPIRED"）
   */
  std::string GetStateString() const;

  // ========== 节点基本操作（CRUD） ==========
  
  /**
   * @brief 创建 ZooKeeper 节点
   * @param path 节点路径（如 "/rpc/UserService"）
   * @param data 节点数据
   * @param node_type 节点类型（持久/临时/顺序）
   * @return true 创建成功, false 创建失败
   * @details
   * - 如果节点已存在，返回 true（幂等操作）
   * - 自动创建父节点（递归创建）
   * - 临时节点会在会话结束时自动删除
   */
  bool Create(const std::string& path, 
              const std::string& data, 
              ZkNodeType node_type = ZkNodeType::PERSISTENT);

  /**
   * @brief 获取节点数据
   * @param path 节点路径
   * @param data [输出] 节点数据
   * @param watch 是否设置 Watch（监听节点变化）
   * @return true 获取成功, false 获取失败
   */
  bool Get(const std::string& path, std::string& data, bool watch = false);

  /**
   * @brief 设置节点数据
   * @param path 节点路径
   * @param data 新数据
   * @return true 设置成功, false 设置失败
   */
  bool Set(const std::string& path, const std::string& data);

  /**
   * @brief 删除节点
   * @param path 节点路径
   * @param recursive 是否递归删除子节点
   * @return true 删除成功, false 删除失败
   */
  bool Delete(const std::string& path, bool recursive = false);

  /**
   * @brief 检查节点是否存在
   * @param path 节点路径
   * @return true 存在, false 不存在
   */
  bool Exists(const std::string& path);

  /**
   * @brief 获取子节点列表
   * @param path 父节点路径
   * @param children [输出] 子节点名称列表
   * @param watch 是否设置 Watch
   * @return true 成功, false 失败
   */
  bool GetChildren(const std::string& path, 
                   std::vector<std::string>& children, 
                   bool watch = false);

  // ========== RPC 服务注册与发现 ==========
  
  /**
   * @brief 注册 RPC 服务到 ZooKeeper
   * @param service_name 服务名称（如 "UserService"）
   * @param service_addr 服务地址（如 "192.168.1.100:8080"）
   * @return true 注册成功, false 注册失败
   * @details
   * 注册路径：/rpc/{service_name}/{service_addr}
   * 节点类型：临时节点（服务下线自动删除）
   * 
   * @example
   * zk.RegisterService("UserService", "192.168.1.100:8080");
   * // 创建节点：/rpc/UserService/192.168.1.100:8080
   */
  bool RegisterService(const std::string& service_name, 
                      const std::string& service_addr);

  /**
   * @brief 注销 RPC 服务
   * @param service_name 服务名称
   * @param service_addr 服务地址
   * @return true 注销成功, false 注销失败
   */
  bool UnregisterService(const std::string& service_name, 
                        const std::string& service_addr);

  /**
   * @brief 获取指定服务的所有实例列表
   * @param service_name 服务名称
   * @return 服务地址列表（如 ["192.168.1.100:8080", "192.168.1.101:8080"]）
   * @details
   * 查询路径：/rpc/{service_name}/*
   * 返回所有子节点名称（即服务地址）
   * 
   * @example
   * auto services = zk.GetServiceList("UserService");
   * // 返回：["192.168.1.100:8080", "192.168.1.101:8080"]
   */
  std::vector<std::string> GetServiceList(const std::string& service_name);

  /**
   * @brief 监听服务变化（Watch 机制）
   * @param service_name 服务名称
   * @param callback 服务变化时的回调函数
   * @return true 监听成功, false 监听失败
   * @details
   * 当服务上线/下线时，触发回调通知
   * 
   * @example
   * zk.WatchService("UserService", [](const std::string& path, int type, int state) {
   *   if (type == ZOO_CHILD_EVENT) {
   *     std::cout << "Service list changed!" << std::endl;
   *     // 重新获取服务列表
   *     auto services = zk.GetServiceList("UserService");
   *   }
   * });
   */
  bool WatchService(const std::string& service_name, ZkWatchCallback callback);

  // ========== Watch 监听机制 ==========
  
  /**
   * @brief 设置自定义 Watch 回调
   * @param path 监听的节点路径
   * @param callback 回调函数
   */
  void SetWatcher(const std::string& path, ZkWatchCallback callback);

  /**
   * @brief 清除指定路径的 Watch 回调
   * @param path 节点路径
   */
  void ClearWatcher(const std::string& path);

  // ========== 辅助功能 ==========
  
  /**
   * @brief 打印 ZooKeeper 树形结构（调试用）
   * @param root_path 根路径
   * @param max_depth 最大深度（-1 表示无限制）
   */
  void PrintTree(const std::string& root_path = "/", int max_depth = 5);

 private:
  // ========== 私有构造/析构（单例模式） ==========
  
  ZkClient();
  ~ZkClient();

  // 禁止拷贝和赋值
  ZkClient(const ZkClient&) = delete;
  ZkClient& operator=(const ZkClient&) = delete;

  // ========== 内部方法 ==========
  
  /**
   * @brief 全局 Watcher 回调（ZooKeeper C API 要求）
   * @details 这是静态函数，会分发到实例方法 OnWatcherEvent
   */
  static void GlobalWatcher(zhandle_t* zh, int type, int state, 
                           const char* path, void* context);

  /**
   * @brief 处理 Watcher 事件（实例方法）
   * @param type 事件类型
   * @param state 连接状态
   * @param path 节点路径
   */
  void OnWatcherEvent(int type, int state, const std::string& path);

  /**
   * @brief 重连机制
   * @return true 重连成功, false 重连失败
   */
  bool Reconnect();

  /**
   * @brief 递归创建父节点
   * @param path 节点路径
   * @return true 成功, false 失败
   */
  bool CreateParentNodes(const std::string& path);

  /**
   * @brief 递归删除节点及其子节点
   * @param path 节点路径
   * @return true 成功, false 失败
   */
  bool DeleteRecursive(const std::string& path);

  /**
   * @brief 递归打印树形结构（内部实现）
   */
  void PrintTreeRecursive(const std::string& path, int depth, int max_depth, 
                         const std::string& prefix);

  /**
   * @brief 将 ZooKeeper 错误码转换为字符串
   * @param error_code 错误码
   * @return 错误描述
   */
  std::string ErrorToString(int error_code) const;

  // ========== 成员变量 ==========
  
  ZkConfig config_;                   // 配置参数
  zhandle_t* zk_handle_;              // ZooKeeper 客户端句柄
  
  // 连接状态管理
  std::atomic<bool> is_connected_;    // 是否已连接
  std::atomic<int> connection_state_; // 连接状态（ZOO_CONNECTED_STATE 等）
  std::mutex connect_mutex_;          // 连接锁
  std::condition_variable connect_cv_;// 连接条件变量

  // Watch 回调管理
  std::mutex watchers_mutex_;         // Watch 回调锁
  std::unordered_map<std::string, ZkWatchCallback> watchers_;  // 路径 -> 回调映射

  // 统计信息
  std::atomic<uint64_t> total_operations_{0};  // 总操作次数
  std::atomic<uint64_t> failed_operations_{0}; // 失败次数
};