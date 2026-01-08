#include "rpcclient.h"
#include "rpcheader.pb.h" 
#include "mprpcapplication.h" 
#include "zookeeperutil.h"    

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <atomic>

#include <google/protobuf/io/coded_stream.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>

// ============================================================================
// 静态辅助工具 
// (保留原来源码逻辑，未修改，仅作为工具函数使用)
// ============================================================================
/**
 * @brief 从字符串缓冲区中偷看（不消费）一个 varint32 值
 * @details
 * Varint 是 Protobuf 的变长整数编码：
 * - 小于 128 的数用 1 字节编码
 * - 大数用 2-5 字节编码
 * 这个函数只读取但不移动缓冲区指针，用于判断数据是否完整
 * 
 * @param buffer 数据缓冲区
 * @param offset 从哪个位置开始读取
 * @param value [输出] 解析出的 varint32 值
 * @param varint_size [输出] 这个 varint 占用了多少字节
 * @return true 成功读取, false 数据不完整
 */
static bool PeekVarint32FromString(const std::string& buffer, 
                                   size_t offset,
                                   uint32_t& value, 
                                   size_t& varint_size) {
  // 边界检查
  if (offset >= buffer.size()) return false;

  const char* data = buffer.data() + offset;
  size_t readable = buffer.size() - offset;
  
  if (readable == 0) return false;

  // Varint32 最多占用 5 字节，我们只读取当前可用的数据
  google::protobuf::io::ArrayInputStream array_input(
      data, std::min(readable, size_t(5)));
  google::protobuf::io::CodedInputStream coded_input(&array_input); //把coded_input指向array_input，设置为只能读取长度大于5字节的数据
  
  // 尝试读取 varint32
  if (!coded_input.ReadVarint32(&value)) {
    return false;  // 数据不够，无法完整读取 varint
  }
  
  // 记录这个 varint 实际占用的字节数
  varint_size = coded_input.CurrentPosition();
  return true;
}

// ============================================================================
// [类 RpcConnection] 实现 
// 【并发化修改】：这是一个新抽象出的类，用于封装单个 TCP 连接的生命周期和 IO 线程
// ============================================================================

RpcConnection::RpcConnection(int id, const std::string& ip, uint16_t port,
                             const RpcClientConfig& config)
    : id_(id), ip_(ip), port_(port), config_(config), fd_(-1) {}

RpcConnection::~RpcConnection() {
    StopReceiveThread();
    Close();
}

/**
 * @brief [RpcConnection] 建立连接
 * @details 
 * 逻辑保留了原 MprpcChannel::Connect 的核心实现：
 * 1. 非阻塞 Socket
 * 2. connect() 调用
 * 3. select() 超时等待
 * * 【修改说明】：代码从 MprpcChannel 移动到了 RpcConnection，逻辑基本不变。
/**
 * @brief 建立 TCP 连接到服务端
 * @details
 * 实现要点：
 * 1. 使用非阻塞 socket + select 实现连接超时控制
 * 2. 连接成功后恢复阻塞模式
 * 3. 设置读写超时，防止 recv/send 无限阻塞
 * 
 * @param ip IP 地址（点分十进制字符串）
 * @param port 端口号
 * @param err_msg [输出] 错误信息
 * @return true 连接成功, false 连接失败
 */
bool RpcConnection::Connect() {
    // 1. 创建 socket（创建的是一个socket描述符）
    int client_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (client_fd == -1) {
        LOG_ERROR("[Conn-{}] socket() failed: {}", id_, strerror(errno));
        // std::cerr << "[Conn-" << id_ << "] socket() failed: " << strerror(errno) << std::endl;
        return false;
    }

    // 2. 设置非阻塞模式（用于连接超时控制）
    int flags = fcntl(client_fd, F_GETFL, 0);
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // 3. 连接服务器
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port_);
    server_addr.sin_addr.s_addr = inet_addr(ip_.c_str());

    int ret = connect(client_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)); // Linux 提供的全局函数 connect()
    
    // 非阻塞 connect 会立即返回 -1，errno 为 EINPROGRESS
    if (ret == -1 && errno != EINPROGRESS) {

        LOG_ERROR("[Conn-{}] connect() failed: {}", id_, strerror(errno));
        // std::cerr << "[Conn-" << id_ << "] connect() failed: " << strerror(errno) << std::endl;
        close(client_fd);
        return false;
    }

    // 4. 使用 select 等待连接完成（带超时）
    if (ret == -1) {
        fd_set write_fds;
        FD_ZERO(&write_fds);
        FD_SET(client_fd, &write_fds);

        // 设置超时时间
        struct timeval timeout;
        timeout.tv_sec = config_.connect_timeout_ms / 1000;    // 配置时间（毫秒）转换为秒部分
        timeout.tv_usec = (config_.connect_timeout_ms % 1000) * 1000;  // 将毫秒的余数转换为微秒

        // select 监听 socket 可写（表示connect连接完成）
        ret = select(client_fd + 1, nullptr, &write_fds, nullptr, &timeout);

        if (ret <= 0) {
            LOG_ERROR("[Conn-{}] connect timeout/error", id_);
            // std::cerr << "[Conn-" << id_ << "] connect timeout/error" << std::endl;
            close(client_fd);
            return false;
        }

        // 检查 socket 错误状态（可能连接失败但 socket 可写）
        int error = 0;
        socklen_t len = sizeof(error);
        if (getsockopt(client_fd, SOL_SOCKET, SO_ERROR, &error, &len) < 0 || error != 0) {
            LOG_ERROR("[Conn-{}] connect socket error: {}", id_, strerror(error));
            // std::cerr << "[Conn-" << id_ << "] connect socket error: " << strerror(error) << std::endl;
            close(client_fd);
            return false;
        }
    }

    // 5. 恢复阻塞模式 (后续 send/recv 使用阻塞模式，但 recv 在独立线程中)
    fcntl(client_fd, F_SETFL, flags);
    
    // 6. 设置读写超时 (防止死锁)
    struct timeval timeout;
    timeout.tv_sec = config_.rpc_timeout_ms / 1000;
    timeout.tv_usec = (config_.rpc_timeout_ms % 1000) * 1000;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)); // 设置接收超时
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)); // 设置发送超时

    fd_ = client_fd;
    LOG_INFO("[Conn-{}] Connected to {}:{}", id_, ip_, port_);
    // std::cout << "[Conn-" << id_ << "] Connected to " << ip_ << ":" << port_ << std::endl;
    return true;
}

void RpcConnection::Close() {
    if (fd_ != -1) {
        close(fd_);
        fd_ = -1;
    }
}

/**
 * @brief [RpcConnection] 发送请求
 * @details
 * 逻辑保留了原 MprpcChannel::SendRequest 的序列化和拼接逻辑。
 * * 【并发化修改】：
 * 1. 增加了 `std::lock_guard<std::mutex> lock(send_mutex_);` 
 * 原因：多个业务线程可能通过连接池共享同一个 Connection 对象，必须互斥写入 socket。
 * 2. 不再负责“接收响应”，发送完毕即返回 true。
 * 
 * @brief 发送 RPC 请求（带帧头）
 * @details
 * 协议格式：[Varint32: header_size] + [RpcHeader] + [Args]
 * 
 * RpcHeader 包含：
 * - service_name: 服务名
 * - method_name: 方法名
 * - args_size: 参数长度
 * - request_id: 请求 ID（用于异步匹配）
 * 
 * @param request_id 请求 ID
 * @param service_name 服务名
 * @param method_name 方法名
 * @param request 请求对象
 * @param controller 控制器
 * @return true 发送成功, false 发送失败
 */
bool RpcConnection::SendRequest(uint64_t request_id,
                                const std::string& service_name,
                                const std::string& method_name,
                                const google::protobuf::Message* request,
                                google::protobuf::RpcController* controller) {
    // 【关键修改】加锁保护发送缓冲区和 Socket 写入
    std::lock_guard<std::mutex> lock(send_mutex_);
    
    // 1. 序列化请求参数 Request
    std::string args_str;
    if (!request->SerializeToString(&args_str)) {
        controller->SetFailed("Serialize request failed");
        return false;
    }

    // 2. 构造 Header
    RPC::RpcHeader header;
    header.set_service_name(service_name);
    header.set_method_name(method_name);
    header.set_args_size(args_str.size());
    header.set_request_id(request_id);  // 关键：设置请求 ID

    // 3. 序列化 RpcHeader
    std::string header_str;
    if (!header.SerializeToString(&header_str)) {
        controller->SetFailed("Serialize header failed");
        return false;
    }

    // 4. 构造完整的请求帧：[Varint32: header_size] + [Header] + [Args]
    std::string send_buffer;
    {
        google::protobuf::io::StringOutputStream string_output(&send_buffer); // 将 string 包装成 protobuf 支持的输出流
        google::protobuf::io::CodedOutputStream coded_output(&string_output); // 给“流”增加了“编码能力”和“缓冲能力”
        
        // 写入 header 长度（varint 编码，占 1-5 字节）
        coded_output.WriteVarint32(header_str.size());
        // CodedOutputStream 析构时会自动 flush，coded_output会让适配器string_output赶紧取走数据（header长度）
    }
    
    // 追加 header 和 args
    send_buffer.append(header_str);
    send_buffer.append(args_str);

    // 5. 发送数据（循环发送，处理部分发送的情况）
    size_t total_sent = 0;
    while (total_sent < send_buffer.size()) {
        ssize_t sent = send(fd_, send_buffer.data() + total_sent,
                            send_buffer.size() - total_sent, 0);
        if (sent <= 0) {
            controller->SetFailed(std::string("Send failed: ") + strerror(errno));
            failed_requests_++;
            Close(); // 发送失败视为连接断开
            return false;
        }
        total_sent += sent;
    }
    total_requests_++;
    return true;
}

/**
 * @brief [RpcConnection] 启动接收线程
 * @details
 * 【并发化修改】：这是新增的函数。
 * 以前的接收是在 CallMethod 中同步调用的，现在每个连接启动一个独立的 std::thread 进行接收。
 * 解决了并发重复启动导致的崩溃问题
 */
void RpcConnection::StartReceiveThread(std::function<void(uint64_t, int32_t, const std::string&, const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(thread_start_mutex_);  // 加锁防止多线程同时启动

    // 如果已经在运行，直接返回，不做任何操作
    // 这让后续拿到该连接的线程可以直接复用已有的接收线程
    if (thread_is_running_) {
        LOG_INFO("[Conn-{}] Receive thread already running, can be reused", id_);
        return;
    }

    response_callback_ = callback; // 绑定回调，连接层只负责收字节，收齐了就通过这个回调扔给 Channel 去处理业务。
    stop_recv_thread_ = false;
    recv_thread_ = std::thread([this]() { ReceiveLoop(); }); // 启动线程：std::thread 启动，执行 ReceiveLoop

    // 标记为已运行
    thread_is_running_ = true;

    LOG_INFO("[Conn-{}] Receive thread first started.", id_);
}

void RpcConnection::StopReceiveThread() {
    stop_recv_thread_ = true;   // 通知线程在下一次循环判断时退出（优雅退出）。
    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }

    std::lock_guard<std::mutex> lock(thread_start_mutex_);
    thread_is_running_ = false;
}

/**
 * @brief [RpcConnection] 接收线程循环
 * @details
 * 【并发化修改】：
 * 1. 这是一个死循环 (`while(!stop)` )，持续从 socket 读取数据。
 * 2. 复用了原 `ReadToBuffer` 和 `TryParseResponse` 的核心逻辑。
 * 3. 当解析出一个完整包时，不再直接处理，而是通过 `response_callback_` 通知 MprpcChannel。
 */
void RpcConnection::ReceiveLoop() {
    while (!stop_recv_thread_) {
        // 1. 读取数据 (复用逻辑)
        if (!ReadToBuffer()) {
            if (!stop_recv_thread_) {
                LOG_ERROR("[Conn-{}] Connection closed/error, waiting for retry...", id_);
                // std::cerr << "[Conn-" << id_ << "] Connection closed/error, waiting for retry..." << std::endl;
                Close();
                // 简单重试等待，防止 CPU 空转
                std::this_thread::sleep_for(std::chrono::seconds(1)); 
            }
            break;
        }

        // 2. 循环解析 (处理粘包)
        while (true) {
            uint64_t request_id;
            int32_t error_code;
            std::string error_msg, response_data;
            bool parse_success = false;

            // 如果 TryParseResponse 内部使用了 throw（如旧代码逻辑），这里会捕获异常
            try{
                // 加锁读取 buffer
                std::lock_guard<std::mutex> lock(recv_mutex_);
                // 尝试解析一个完整包
                parse_success = TryParseResponse(request_id, error_code, error_msg, response_data);
            }
            catch (const std::exception& e) {
                // 捕获标准异常 (如 bad_alloc, runtime_error)
                LOG_ERROR("[Conn-{}] CRITICAL EXCEPTION in parser: {}", id_, e.what());
                // std::cerr << "[Conn-" << id_ << "] CRITICAL EXCEPTION in parser: " << e.what() << std::endl;
                // 发生异常通常意味着内存错乱或协议严重破坏，必须断开连接
                Close(); 
                return; // 直接退出线程，防止死循环或二次崩溃
            }
            catch (...) {
                // 捕获未知异常
                LOG_ERROR("[Conn-{}] UNKNOWN EXCEPTION in parser.", id_);
                // std::cerr << "[Conn-" << id_ << "] UNKNOWN EXCEPTION in parser." << std::endl;
                Close();
                return;
            }

            if (!parse_success) {
                break; // 数据不够，跳出内层循环，继续 ReadToBuffer
            }

            // 3. 触发回调 (通知 Channel 层)
            if (response_callback_) {
                response_callback_(request_id, error_code, error_msg, response_data);
            }
        }
    }
    LOG_INFO("[Conn-{}] Receive thread exited.", id_);

}

/**
 * @brief [RpcConnection] 读取数据到缓冲区
 * @details
 * 逻辑基本保留原 MprpcChannel::ReadToBuffer。
 * 【并发化修改】：
 * 增加了 `std::lock_guard<std::mutex> lock(recv_mutex_);`
 * 因为接收线程在写 buffer，而可能的外部监控或重置操作可能会读写 buffer。
 */
bool RpcConnection::ReadToBuffer() {
    if (fd_ == -1) return false;

    char temp_buf[4096];
    ssize_t n = recv(fd_, temp_buf, sizeof(temp_buf), 0);
    
    if (n > 0) {
            // 1. 正常读取到数据
            std::lock_guard<std::mutex> lock(recv_mutex_);
            recv_buffer_.append(temp_buf, n);
            return true;
        } 
        else if (n == 0) {
            // 2. 对端关闭连接 (FIN)
            LOG_ERROR("[Conn-{}] Connection closed by peer.", id_);
            // std::cerr << "[Conn-" << id_ << "] Connection closed by peer." << std::endl;
            return false; // 返回 false 通知调用者断开连接
        } 
        else {
            // 3. 读取发生错误 (n < 0)
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                // 3.1 仅仅是超时 (Timeout) 或 信号中断
                // 注意：对于连接池的长连接，超时不应该视为错误断开，
                // 而是应该视为空转一轮，让线程有机会检查 stop_recv_thread_ 标志。
                // 所以这里返回 true，但是不追加数据。
                return true; 
            } else {
                // 3.2 真正的 socket 错误 (如 Connection Reset)
                LOG_ERROR("[Conn-{}] Recv error: {}", id_, strerror(errno));
                // std::cerr << "[Conn-" << id_ << "] Recv error: " << strerror(errno) << std::endl;
                return false; // 返回 false 通知调用者断开连接
            }
        }
}

/**
 * @brief [RpcConnection] 解析响应
 * @details
 * 逻辑完全保留原 MprpcChannel::TryParseResponse 的 Peek-Check-Consume 机制。
 * 负责从二进制流中切分出完整的 [Header + Body]。
 */
bool RpcConnection::TryParseResponse(uint64_t& request_id, int32_t& error_code,
                                     std::string& error_msg, std::string& response_data) {                                     
  // 注意：调用者已对 m_recv_mutex 加锁

  // ========== 阶段 1: Peek Varint32 (Header 长度) ==========
  size_t offset = 0;
  uint32_t header_size = 0;
  size_t varint_size = 0;

  if (!PeekVarint32FromString(recv_buffer_, offset, header_size, varint_size)) {
    // 数据不够，连 varint 都读不出来
    return false;
  }

  // 校验 header 大小（防止恶意大包）
  if (header_size == 0 || header_size > config_.max_message_size) {
    LOG_ERROR("[Conn-{}] Invalid header size: {}", id_, header_size);
    // 丢弃损坏的数据
    recv_buffer_.erase(0, varint_size);
    throw std::runtime_error("Invalid response header size");
  }

  offset += varint_size;

  // ========== 阶段 2: 检查是否有完整的 Header ==========
  if (recv_buffer_.size() < offset + header_size) {
    // Header 不完整，等待更多数据
    return false;
  }

  // ========== 阶段 3: 解析 RpcHeader ==========
  std::string header_str = recv_buffer_.substr(offset, header_size);
  offset += header_size;

  RPC::RpcHeader rpc_header;
  if (!rpc_header.ParseFromString(header_str)) {
    LOG_ERROR("[Conn-{}] Failed to parse RPC header", id_);
    recv_buffer_.erase(0, offset);
    throw std::runtime_error("Invalid RPC header");
  }

  // 提取 Header 中的信息
  request_id = rpc_header.request_id();
  error_code = rpc_header.error_code();
  error_msg = rpc_header.error_msg();
  uint32_t args_size = rpc_header.args_size();

  // 校验 args_size
  if (args_size > config_.max_message_size) {
    LOG_ERROR("[Conn-{}] Args size too large: {}", id_, args_size);
    recv_buffer_.erase(0, offset);
    throw std::runtime_error("Response too large");
  }

  // ========== 阶段 4: 检查是否有完整的 Response Data ==========
  if (recv_buffer_.size() < offset + args_size) {
    // Response Data 不完整，等待更多数据
    return false;
  }

  // ========== 阶段 5: 读取 Response Data ==========
  response_data = recv_buffer_.substr(offset, args_size);
  offset += args_size;

  // ========== 阶段 6: 消费已处理的数据 ==========
  recv_buffer_.erase(0, offset);

    //   //【调试日志】
    // std::cout << "[TryParseResponse] Parsed response for request " << request_id
    //         << ", error_code=" << error_code 
    //         << ", data size=" << response_data.size() << std::endl;

  return true;
}

// ============================================================================
// [类 ConnectionPool] 实现
// 【并发化修改】：这是一个新类，用于管理多个 RpcConnection，实现负载均衡
// ============================================================================

ConnectionPool::ConnectionPool(const std::string& ip, uint16_t port,
                               const RpcClientConfig& config)
    : ip_(ip), port_(port), config_(config) {}

ConnectionPool::~ConnectionPool() {
    // 优雅结束所有连接的接收线程
    for (auto& conn : connections_) {
        conn->StopReceiveThread();
    }
}

/**
 * @brief [ConnectionPool] 初始化连接池
 * @details 创建指定数量的 RpcConnection 并尝试连接。
 */
bool ConnectionPool::Init() {
    LOG_INFO("[ConnPool] Initializing connection pool to {}:{} with {} connections.",
             ip_, port_, config_.connection_pool_size);
    for (int i = 0; i < config_.connection_pool_size; ++i) {
        auto conn = std::make_shared<RpcConnection>(i, ip_, port_, config_);    // 创建N个连接对象实例
        if (!conn->Connect()) {
            LOG_ERROR("[ConnPool] Warn: Failed to connect {}, will retry later.", i);
            // std::cerr << "[ConnPool] Warn: Failed to connect " << i << ", will retry later." << std::endl;
            // 策略：即使部分连接失败也继续初始化，允许后续重连
        }
        LOG_INFO("[ConnPool] Successfully created connection {}", i);
        connections_.push_back(conn); // 创建的连接加入连接池（一个 vector 里）
    }
    return true;
}

/**
 * @brief [ConnectionPool] 获取连接
 * @details 简单的 Round-Robin (轮询) 策略，实现负载均衡。
 * 原子计数器让n个线程分别获取到0~n-1的返回值，再对每个值进行取模映射到连接池索引，索引对应的是真正的连接，将被返回分配。
 */
std::shared_ptr<RpcConnection> ConnectionPool::GetConnection() {
    if (connections_.empty()) return nullptr;
    // 线程安全的轮询
    size_t idx = next_conn_idx_.fetch_add(1) % connections_.size();
    return connections_[idx];   // 从连接池中取出对应索引的连接
}

// 打印连接池中每个连接的统计信息
void ConnectionPool::PrintStats() const {
    LOG_INFO("========== Connection Pool Stats ==========");
    for (const auto& conn : connections_) {
        LOG_INFO("Conn-{} | Requests: {} | Failed: {}",
                 conn->GetId(), conn->GetTotalRequests(), conn->GetFailedRequests());
    }
    LOG_INFO("==========================================");
}

// ============================================================================
// [类 MprpcChannel] 实现 (核心入口)
// ============================================================================

// 全局连接池 Map，用于缓存不同 IP:Port 的连接池
// Key: "127.0.0.1:8000"
static std::map<std::string, std::shared_ptr<ConnectionPool>> g_conn_pools;
static std::mutex g_pools_mutex;
// 初始化静态成员
std::unordered_map<uint64_t, std::shared_ptr<PendingRpcContext>> MprpcChannel::pending_requests_;
std::mutex MprpcChannel::pending_mutex_;

// 构造函数不再强制初始化单个 IP 的 Pool，而是作为 RPCClient 启动器
// ip 和 port 参数现在可以传空，或者用于直连模式
MprpcChannel::MprpcChannel(const std::string& ip, uint16_t port,
                           const RpcClientConfig& config)
    : ip_(ip), port_(port), config_(config) {

    // 1. 如果构造时指定了 IP，则初始化直连池
    if (!ip_.empty() && port_ != 0) {
        conn_pool_ = std::make_unique<ConnectionPool>(ip, port, config);
        conn_pool_->Init();
        
        // 这里使用简单的 Hack 方式，循环调用 GetConnection 来覆盖所有连接。
        // 给每个连接注册同一个回调函数：MprpcChannel::OnResponseReceived
        for(int i = 0; i < config.connection_pool_size * 2; ++i) { 
            auto conn = conn_pool_->GetConnection();
            // 启动接受线程
            conn->StartReceiveThread([this](uint64_t req_id, int32_t err, const std::string& msg, const std::string& data) {
                this->OnResponseReceived(req_id, err, msg, data);
            });
        }
    }

    // 3. 启动超时检查后台线程
    stop_timeout_checker_ = false;
    timeout_checker_thread_ = std::thread([this]() { TimeoutCheckerLoop(); });

    // =======================================================================
    // 注册优雅关闭钩子 Hook
    // =======================================================================
    // 防止程序被 kill 时，析构函数没执行，导致线程没 join
    shutdown_hook_id_ = MprpcApplication::GetInstance().RegisterShutdownHook([this]() {
        LOG_INFO("[MprpcChannel] Shutdown hook triggered.");
        this->Shutdown(); // 调用提取出来的 Shutdown 方法
    });
}

MprpcChannel::~MprpcChannel() {
    // 注销钩子 & 执行关闭
    // 如果对象是提前正常析构的，必须把钩子摘掉，否则 App 退出时会回调野指针
    if (shutdown_hook_id_ >= 0) {
        LOG_INFO("[MprpcChannel] 在MprpcApplication之前析构，注销钩子避免野指针.");
        MprpcApplication::GetInstance().UnregisterShutdownHook(shutdown_hook_id_);
    }
    
    Shutdown(); // 复用关闭逻辑

    // 注意：g_conn_pools 是全局的，这里不清除，以便其他 Channel 复用连接
    // 如果需要清理，可以在程序退出时统一清理
}

/**
* @brief 优雅关闭
* 停止超时检查线程，停止所有连接池的线程
*/
void MprpcChannel::Shutdown() {
    LOG_INFO("[MprpcChannel] Shutting down...");
    
    // 1. 停止超时检查线程
    if (!stop_timeout_checker_) { // 简单的状态检查，防止重复 join
        stop_timeout_checker_ = true;
        if (timeout_checker_thread_.joinable()) {
            timeout_checker_thread_.join();
        }
    }

    // 2. 停止所有连接池的线程
    // 注意：g_conn_pools 是全局的，通常由程序退出时统一清理。
    // 但如果是直连模式的 conn_pool_ (unique_ptr)，它会随着 MprpcChannel 析构自动释放
    // ConnectionPool 的析构函数里已经调用了 StopReceiveThread。
    // 所以这里主要处理属于自己的线程资源。
    
    if (conn_pool_) {
        // 显式停止，虽然 unique_ptr 析构也会做，但这样更安全
        // conn_pool_.reset(); // 或者什么都不做，依赖析构
    }
    LOG_INFO("[MprpcChannel] MprpcChannel Shutdown 完毕");
}

/**
 * @brief [MprpcChannel] RPC 调用统一入口
 * @details
 * 这是改动最大的函数。
 * * 【并发化修改】：
 * 1. **连接获取**：不再使用单一 fd，而是从连接池获取连接 `conn_pool_->GetConnection()`。
 * 2. **异步发送**：调用 `conn->SendRequest` 后不再立即调用 `ReceiveResponse`。
 * 3. **同步等待**：
 * - 原逻辑：Send -> Receive(阻塞读) -> Return
 * - 新逻辑：Send -> `cv.wait_for`(挂起等待信号) -> Return
 * - 信号由 IO 线程在 `OnResponseReceived` 中触发。
 * 4. **异步支持**：如果 `done != nullptr`，发送完直接返回，不阻塞。
 * 5. 引入 ZK 服务发现
 */
void MprpcChannel::CallMethod(const google::protobuf::MethodDescriptor* method,
                              google::protobuf::RpcController* controller,
                              const google::protobuf::Message* request,
                              google::protobuf::Message* response,
                              google::protobuf::Closure* done) {
    // 获取服务名和方法名
    std::string service_name = method->service()->name();
    std::string method_name = method->name();

    // -------------------------------------------------------------
    // ZooKeeper 服务发现逻辑
    // -------------------------------------------------------------
    std::string target_ip = ip_;
    uint16_t target_port = port_;

    // 如果没有指定直连 IP，则通过 ZK 查询
    if (target_ip.empty()) {
        ZkClient& zk = ZkClient::GetInstance();
        // 确保 RPC 客户端的 ZK 客户端已启动
        if (!zk.IsConnected()) {
            ZkConfig zk_conf;
            zk_conf.host = MprpcApplication::GetInstance().GetConfig().Load("zookeeper_ip");    // 从配置文件获取 ZK 服务器地址
            zk_conf.host += ":" + MprpcApplication::GetInstance().GetConfig().Load("zookeeper_port"); // 追加端口
            zk_conf.root_path = "/mprpc";
            zk.Init(zk_conf);  
            zk.Start(); // 启动 ZK 客户端连接
        }

        // 获取远程 Zk 服务端中service_name这个服务的的服务列表
        std::vector<std::string> hosts = zk.GetServiceList(service_name);
        if (hosts.empty()) {
            controller->SetFailed("No service provider found for: " + service_name);
            if (done) done->Run();
            return;
        }

        // 轮询负载均衡算法：选择一个提供 server_name 服务的 IP:Port
        // 使用静态原子变量，确保多线程安全且不需要修改类成员
        // 每次请求到来，索引加 1
        static std::atomic_uint load_balance_idx{0};
        
        // 取模运算，确保索引落在 hosts 范围内
        int idx = load_balance_idx.fetch_add(1) % hosts.size();
        
        std::string host_data = hosts[idx]; // 获取选中的节点

        // =============================================================

        size_t split = host_data.find(':');
        if (split == std::string::npos) {
            controller->SetFailed("Invalid host address from ZK: " + host_data);
            return;
        }
        target_ip = host_data.substr(0, split);  // 服务实例的IP
        target_port = atoi(host_data.substr(split + 1).c_str());   // 服务实例的Port
    }

    // 生成全局唯一的 Request ID
    uint64_t request_id = GenerateRequestId();

    // 创建上下文，保存到全局 Map 中
    auto ctx = std::make_shared<PendingRpcContext>();
    ctx->request_id = request_id;
    ctx->response = response;
    ctx->controller = controller;
    ctx->done = done;
    ctx->start_time = std::chrono::steady_clock::now();

    RegisterPendingRequest(request_id, ctx);

    // 获取或创建对应的连接池
    std::shared_ptr<RpcConnection> conn;
    // 如果是直连模式 (构造函数传了IP)，直接用成员变量 conn_pool_
    if (!ip_.empty()) {
        conn = conn_pool_->GetConnection();
    } else {
        // 否则使用动态全局池
        std::string host_key = target_ip + ":" + std::to_string(target_port); // 能够提供该服务的实例IP:Port
        std::shared_ptr<ConnectionPool> pool;
        
        {
            std::lock_guard<std::mutex> lock(g_pools_mutex);
            auto it = g_conn_pools.find(host_key);  // 全局名册 (`g_conn_pools`)是否已经有该 IP 的连接池
            if (it == g_conn_pools.end()) {
                // 首次连接该 IP，创建新池
                pool = std::make_shared<ConnectionPool>(target_ip, target_port, config_);
                pool->Init();
                // 必须为新池中的连接绑定当前 Channel 的回调
                // 注意：这里会有个小问题，如果多个 Channel 实例共用一个全局池，回调给谁？
                // 这里的 Hack 方案是：每个连接接收到数据时，根据 request_id 在 Global Map 中找 Context。
                // 但目前的 PendingMap 是成员变量。
                // ----------------------------------------------------
                // 修正：为了支持 ZK 动态连接且不破坏现有的成员变量结构，
                // 我们在这里暂时只支持 "每个目标 IP 创建一个临时连接" 或 "为该 Channel 独享该池"。
                // 考虑到高并发代码的复杂性，这里我们采用【从池中取出连接，并动态绑定回调】的策略
                // ----------------------------------------------------
                
                // 为了简化，这里我们暂时只对新池做初始化，回调绑定在 GetConnection 后做
                g_conn_pools[host_key] = pool;
            } else {
                pool = it->second;
            }
        }
        conn = pool->GetConnection();   // 从全局池中借出一个连接
        
        // 这是一个轻量级操作，因为 StartReceiveThread 内部只是赋值 callback
        // 选中的TCP连接开启接受线程
        conn->StartReceiveThread([](uint64_t req_id, int32_t err, const std::string& msg, const std::string& data) {
            MprpcChannel::OnResponseReceived(req_id, err, msg, data); // 绑定到静态回调函数，以便找到对应的 Channel 实例
        });
    }

    if (!conn || !conn->IsConnected()) {
        // 简单的重连尝试
        if (conn && !conn->IsConnected()) {
            conn->Connect();
        }
        if (!conn || !conn->IsConnected()) {
            controller->SetFailed("No connection available");
            // 失败时必须移除 Pending 记录
            {
                std::lock_guard<std::mutex> lock(pending_mutex_);
                pending_requests_.erase(request_id);
            }
            if (done) done->Run();
            return;
        }
    }

    // 发送请求（非阻塞/独立锁）
    ctx->send_time = std::chrono::steady_clock::now();
    if (!conn->SendRequest(request_id, service_name, method_name, request, controller)) {
        // 发送失败回滚
        {
            std::lock_guard<std::mutex> lock(pending_mutex_);
            pending_requests_.erase(request_id);
        }
        if (done) done->Run();
        return;
    }

    // 【同步调用逻辑】
    // 如果没有传入 done 回调，说明用户希望同步等待结果
    if (done == nullptr) {
        std::unique_lock<std::mutex> lock(ctx->mutex);
        auto timeout = std::chrono::milliseconds(config_.rpc_timeout_ms);
        
        // 挂起当前线程，等待 IO 线程唤醒 (ctx->finished == true)
        if (!ctx->cv.wait_for(lock, timeout, [&]{ return ctx->finished; })) {
            controller->SetFailed("RPC call timeout");
            // 超时后的清理工作主要由 TimeoutChecker 负责，这里只负责标记失败
        }
    }
    // 【异步调用逻辑】
    // 如果 done != nullptr，函数直接结束。当 IO 线程收到响应后，会主动调用 done->Run()。
}// <--- CallMethod 函数结束，控制权返回给调用者（上层业务）

/**
 * @brief [MprpcChannel] 响应回调处理
 * @details
 * 该函数由 RpcConnection 的 IO 接收线程调用。
 * 相当于原源码中的 `CompletePendingRequest`，但适配了并发逻辑。
 * * 流程：
 * 1. 查表 (PendingMap) 找到 request_id 对应的上下文。
 * 2. 反序列化响应数据。
 * 3. 唤醒等待的业务线程 (Notify) 或 执行异步回调 (Run)。
 */
void MprpcChannel::OnResponseReceived(uint64_t request_id, int32_t error_code,
                                      const std::string& error_msg,
                                      const std::string& response_data) {
    std::shared_ptr<PendingRpcContext> ctx;
    {
        // 加锁查找并移除，防止重复处理
        std::lock_guard<std::mutex> lock(pending_mutex_);
        auto it = pending_requests_.find(request_id);
        if (it == pending_requests_.end()) {
            return; // 找不到说明可能已经超时被移除了
        }
        ctx = it->second;
        pending_requests_.erase(it);
    }

    // 填充结果
    if (error_code != 0) {
        ctx->controller->SetFailed(error_msg);
    } else {
        if (!ctx->response->ParseFromString(response_data)) {
            ctx->controller->SetFailed("Parse response error");
        }
    }

    // 唤醒同步等待的线程，无论CallMethod中选择同步等待还是异步回调都会执行唤醒，只不过异步回调时会空响而已
    {
        std::lock_guard<std::mutex> lock(ctx->mutex);
        ctx->finished = true;
        ctx->cv.notify_one();   // <--- 这一行代码向当初发起这个请求的 rpcChannel 线程发出了信号！
    }

    // 执行异步回调，转到上层业务层执行回调函数（同步调用时 done 为空，不需要回调函数）
    if (ctx->done) {
        ctx->done->Run();
    }
}

void MprpcChannel::RegisterPendingRequest(uint64_t request_id, 
                                          std::shared_ptr<PendingRpcContext> ctx) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_requests_[request_id] = ctx;
}

/**
 * @brief [MprpcChannel] 超时检查循环
 * @details 后台线程，定期清理超时的请求，防止内存泄漏。
 */
void MprpcChannel::TimeoutCheckerLoop() {
    while (!stop_timeout_checker_) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
        CleanupTimeoutRequests();
    }
}

/**
 * @brief [MprpcChannel] 执行超时清理
 * @details 扫描 Map，找到超时的请求，模拟一次“失败的响应”来唤醒业务线程。
 */
void MprpcChannel::CleanupTimeoutRequests() {
    auto now = std::chrono::steady_clock::now();
    std::vector<uint64_t> timeout_ids;

    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        for (auto it = pending_requests_.begin(); it != pending_requests_.end(); ) {
            auto ctx = it->second;
            auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                now - ctx->start_time).count();
            if (duration > config_.rpc_timeout_ms) {
                timeout_ids.push_back(it->first);
                ++it; // 这里不直接 erase，而是记录 ID
            } else {
                ++it;
            }
        }
    }

    // 针对超时的 ID，调用 OnResponseReceived 模拟超时错误
    // 这样可以复用唤醒逻辑和清理逻辑
    for (uint64_t id : timeout_ids) {
        OnResponseReceived(id, -1, "RPC Timeout", "");
    }
}

void MprpcChannel::PrintStats() const {
    conn_pool_->PrintStats();
}