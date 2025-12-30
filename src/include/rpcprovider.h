#pragma once

#include <google/protobuf/descriptor.h>
#include <google/protobuf/service.h>
#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>
#include <muduo/net/InetAddress.h>
#include <muduo/net/TcpConnection.h>
#include <muduo/net/TcpServer.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

// RPC 协议层面的错误码定义
// 用于在 SendErrorResponse 中告知客户端具体的失败原因
enum RpcErrorCode {
  RPC_SUCCESS = 0,           // 成功
  RPC_PARSE_ERROR = 1,       // 协议头解析失败
  RPC_SERVICE_NOT_FOUND = 2, // 请求的服务名不存在
  RPC_METHOD_NOT_FOUND = 3,  // 请求的方法名不存在
  RPC_INVALID_REQUEST = 4,   // 请求参数反序列化失败
  RPC_INTERNAL_ERROR = 5,    // 服务端内部错误（如序列化响应失败）
  RPC_TIMEOUT = 6,           // (预留) 超时
  RPC_OVERLOAD = 7           // (预留) 服务端过载
};

/**
 * @brief 通用 RPC 服务提供者 (Server 端核心引擎)
 * * 职责：
 * 1. 网络层：基于 Muduo (Reactor模型) 处理高并发 TCP 连接。
 * 2. 协议层：处理 TCP 粘包/半包，解析 [Length][Header][Body] 格式。
 * 3. 业务层：利用 Protobuf 反射机制，动态分发请求到具体的 Service 实现。
 */
class RpcProvider {
 public:
  // 服务端配置项
  struct Config {
    std::string ip;             // 绑定的 IP 地址 (空则自动获取)
    uint16_t port = 0;          // 绑定的端口
    int thread_num = 4;         // IO 线程数 (通常设为 CPU 核数)
    int max_connections = 10000;// 最大并发连接数限制
    
    // 最大消息大小限制 (默认 10MB)
    // 用于防止恶意的大包攻击导致服务端 OOM (内存耗尽)
    uint32_t max_message_size = 10 * 1024 * 1024; 
    
    // 空闲连接超时时间 (默认 300秒)
    // 配合 CheckIdleConnections 清理死连接
    int idle_timeout_seconds = 300;  
    
    bool enable_metrics = true; // 是否开启指标统计
    std::string log_level = "INFO";
  };

  // 构造函数：传入配置，初始化 Environment
  explicit RpcProvider(const Config& config);
  
  // 析构函数：确保资源（如循环线程）正确释放
  ~RpcProvider();

  // 禁止拷贝和移动 (独占网络资源)
  RpcProvider(const RpcProvider&) = delete;
  RpcProvider& operator=(const RpcProvider&) = delete;

  /**
   * @brief 注册 RPC 服务
   * 利用 Protobuf 反射机制，提取 Service 中的所有 Method 信息并建立映射表
   * @param service 业务层实现的 Service 对象 (如 RaftService)
   * @return true 注册成功
   */
  bool NotifyService(google::protobuf::Service* service);

  /**
   * @brief 启动 RPC 服务
   * 这是一个阻塞调用，会启动 EventLoop 并开始监听
   */
  void Run();

  /**
   * @brief 优雅关闭
   * 停止接受新连接，断开现有连接，停止 EventLoop
   */
  void Shutdown();

  // 运行指标 (用于监控)
  struct Metrics {
    std::atomic<uint64_t> total_requests{0};     // 总请求数
    std::atomic<uint64_t> failed_requests{0};    // 失败请求数
    std::atomic<uint64_t> active_connections{0}; // 当前活跃连接数
    std::atomic<uint64_t> partial_messages{0};   // 发生的半包/粘包次数
    std::atomic<int> pending_requests{0};       // 当前正在处理的请求数
  };
  const Metrics& GetMetrics() const { return metrics_; }

 private:
  //测试类是我的朋友，允许它访问我的私有成员
  friend class RpcProviderTester;
  // 服务信息结构体 (注册表项)
  struct ServiceInfo {
    google::protobuf::Service* service; // 服务对象基类指针
    // 方法名 -> 方法描述符 的映射，用于快速查找
    std::unordered_map<std::string, const google::protobuf::MethodDescriptor*> method_map;
  };

  // 连接上下文 (附加到每个 TcpConnection 上)
  struct ConnectionContext {
    muduo::net::TcpConnectionPtr conn;
    muduo::Timestamp last_active_time;   // 最后一次收发数据的时间 (用于心跳超时)
    std::atomic<int> pending_requests{0}; // 当前正在处理的请求数
  };

  // 配置信息
  Config config_;

  // Hook ID，用于注册和注销生命周期钩子
  int shutdown_hook_id_ = -1;  

  // Muduo 网络库核心组件
  muduo::net::EventLoop event_loop_;          // 主事件循环
  std::unique_ptr<muduo::net::TcpServer> server_; // TCP 服务器封装

  // 服务注册表 (key: service_name)
  // 需要互斥锁保护，因为 NotifyService 可能在多线程环境被调用
  mutable std::mutex service_mutex_;
  std::unordered_map<std::string, ServiceInfo> service_map_;

  // 连接管理表 (key: connection_name)
  // 用于心跳检测和连接限制
  mutable std::mutex conn_mutex_;
  std::unordered_map<std::string, std::shared_ptr<ConnectionContext>> connections_;

  // 监控指标
  Metrics metrics_;

  // 关闭标志位 (Atomic)
  std::atomic<bool> shutdown_flag_{false};

  // ================= 回调函数区 (由 Muduo 触发) =================

  // 新连接建立/断开时的回调
  void OnConnection(const muduo::net::TcpConnectionPtr& conn);
  
  // 核心：收到数据时的回调
  // 注意：Muduo 是非阻塞网络库，buffer 中的数据可能是不完整的 (半包) 或 包含多个包 (粘包)
  void OnMessage(const muduo::net::TcpConnectionPtr& conn,
                 muduo::net::Buffer* buffer,
                 muduo::Timestamp timestamp);

  // ================= 核心处理逻辑 =================

  /**
   * @brief 尝试从缓冲区解析一个完整的 RPC 消息
   * @details 实现了 "Peek-All-First" 策略：
   * 只有当 Buffer 中集齐了 [Length] + [Header] + [Body] 时，才返回 true 并消费数据。
   * 否则返回 false，且不移动 Buffer 的读取指针。
   * * @param buffer 网络读缓冲区
   * @param header_size [输出] 解析出的头部长度
   * @param service_name [输出] 解析出的服务名
   * @param method_name [输出] 解析出的方法名
   * @param args_str [输出] 解析出的参数二进制数据
   * @return true 成功解析出一个完整包
   */
  bool TryParseMessage(muduo::net::Buffer* buffer,
                       uint32_t& header_size,
                       std::string& service_name,
                       std::string& method_name,
                       std::string& args_str);

  /**
   * @brief 执行 RPC 业务逻辑
   * @details 
   * 1. 查找 Service 和 Method 描述符
   * 2. 反序列化 Request 对象
   * 3. 绑定 SendRpcResponse 回调
   * 4. 调用 service->CallMethod()
   */
  void HandleRpcRequest(const muduo::net::TcpConnectionPtr& conn,
                        const std::string& service_name,
                        const std::string& method_name,
                        const std::string& args_str);

  // ================= 响应发送区 =================

  // 发送正常响应 (由 Closure 回调触发)
  // 会自动序列化 response 并添加长度头
  void SendRpcResponse(muduo::net::TcpConnectionPtr conn,
                       google::protobuf::Message* response);
  
  // 发送错误响应 (如解析失败、服务未找到)
  void SendErrorResponse(const muduo::net::TcpConnectionPtr& conn,
                         int error_code,
                         const std::string& error_msg);

  // ================= 辅助功能区 =================

  // 定时任务：清理空闲连接
  void CheckIdleConnections();
  
  // 从管理表中移除连接
  void RemoveConnection(const std::string& conn_name);

protected:
  // 获取本机非回环 IP 地址
  std::string GetLocalIP() const;
  
  // 校验配置合法性
  bool ValidateConfig() const;
};