#ifndef RPCCLIENT_H
#define RPCCLIENT_H

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/service.h>
// #include <google/protobuf/rpc_channel.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <string>
#include <functional>
#include <chrono>
#include "rpccontroller.h"
#include "zookeeperutil.h"   

// ============================================================================
// 配置结构体
// ============================================================================
struct RpcClientConfig {
  int connect_timeout_ms = 3000;      // 连接超时 (毫秒)
  int rpc_timeout_ms = 5000;          // RPC 调用超时 (毫秒)
  int max_retry_times = 3;            // 最大重试次数
  int max_message_size = 10 * 1024 * 1024;  // 最大消息 10MB

    // 连接池配置
  int connection_pool_size = 4;        // 连接池大小（建议 = CPU 核数）
  int io_thread_pool_size = 2;         // IO 线程数（接收线程）

  bool enable_auto_reconnect = true;  // 是否自动重连
  bool enable_heartbeat = false;      // 是否启用心跳 (预留)
};

// ============================================================================
// 请求上下文
// ============================================================================
struct PendingRpcContext {
    uint64_t request_id;
    google::protobuf::Message* response;
    google::protobuf::RpcController* controller;
    google::protobuf::Closure* done;
    
    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;
    
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point send_time;
};

// ============================================================================
// 类的前置声明
// ============================================================================
class RpcConnection;
class ConnectionPool;

// ============================================================================
// 单个 TCP 连接封装
// ============================================================================
class RpcConnection {
public:
    explicit RpcConnection(int id, const std::string& ip, uint16_t port,
                           const RpcClientConfig& config);
    ~RpcConnection();

    bool Connect();
    void Close();
    bool IsConnected() const { return fd_ != -1; }
    int GetFd() const { return fd_; }
    int GetId() const { return id_; }

    // 发送请求（线程安全）
    bool SendRequest(uint64_t request_id,
                     const std::string& service_name,
                     const std::string& method_name,
                     const google::protobuf::Message* request,
                     google::protobuf::RpcController* controller);

    // 启动/停止接收线程
    void StartReceiveThread(
        std::function<void(uint64_t, int32_t, const std::string&, const std::string&)> callback);
    void StopReceiveThread();

    uint64_t GetTotalRequests() const { return total_requests_; }
    uint64_t GetFailedRequests() const { return failed_requests_; }

private:
    int id_;
    std::string ip_;
    uint16_t port_;
    RpcClientConfig config_;
    
    int fd_;
    std::mutex send_mutex_;      // 保护发送操作
    std::string recv_buffer_;    // 接收缓冲区
    std::mutex recv_mutex_;      // 保护接收缓冲区
    std::mutex thread_start_mutex_;     // 防止多线程同时启动的锁
    bool thread_is_running_ = false;    // 记录是否已经启动的状态

    std::thread recv_thread_;
    std::atomic<bool> stop_recv_thread_{false};
    std::function<void(uint64_t, int32_t, const std::string&, const std::string&)> response_callback_;

    std::atomic<uint64_t> total_requests_{0};
    std::atomic<uint64_t> failed_requests_{0};

    void ReceiveLoop();
    bool ReadToBuffer();
    bool TryParseResponse(uint64_t& request_id, int32_t& error_code,
                          std::string& error_msg, std::string& response_data);
};

// ============================================================================
// 连接池管理器
// ============================================================================
class ConnectionPool {
public:
    ConnectionPool(const std::string& ip, uint16_t port,
                   const RpcClientConfig& config);
    ~ConnectionPool();

    bool Init();
    std::shared_ptr<RpcConnection> GetConnection();
    void PrintStats() const;

private:
    std::string ip_;
    uint16_t port_;
    RpcClientConfig config_;
    
    std::vector<std::shared_ptr<RpcConnection>> connections_;
    std::atomic<size_t> next_conn_idx_{0};
};

// ============================================================================
// 高并发 RPC 客户端 Channel
// ============================================================================
class MprpcChannel : public google::protobuf::RpcChannel {
public:
    MprpcChannel(const std::string& ip, uint16_t port,
                 const RpcClientConfig& config = RpcClientConfig());
    ~MprpcChannel();

    /**
    * @brief 优雅关闭
    * 停止超时检查线程，停止所有连接池的线程
    */
    void Shutdown();

    // 禁用拷贝
    MprpcChannel(const MprpcChannel&) = delete;
    MprpcChannel& operator=(const MprpcChannel&) = delete;

    // 核心调用接口
    void CallMethod(const google::protobuf::MethodDescriptor* method,
                    google::protobuf::RpcController* controller,
                    const google::protobuf::Message* request,
                    google::protobuf::Message* response,
                    google::protobuf::Closure* done) override;

    void PrintStats() const;

private:
    std::string ip_;
    uint16_t port_;
    RpcClientConfig config_;

    std::unique_ptr<ConnectionPool> conn_pool_;

    int shutdown_hook_id_{-1}; // 优雅关闭钩子 ID

    std::atomic<uint64_t> next_request_id_{1};
    static std::mutex pending_mutex_;
    static std::unordered_map<uint64_t, std::shared_ptr<PendingRpcContext>> pending_requests_;

    std::thread timeout_checker_thread_;
    std::atomic<bool> stop_timeout_checker_{false};

    uint64_t GenerateRequestId() { return next_request_id_.fetch_add(1); }
    
    void RegisterPendingRequest(uint64_t request_id, 
                                std::shared_ptr<PendingRpcContext> ctx);
    
    static void OnResponseReceived(uint64_t request_id, int32_t error_code,
                            const std::string& error_msg,
                            const std::string& response_data);
    
    void TimeoutCheckerLoop();
    void CleanupTimeoutRequests();
};

#endif // RPCCLIENT_H