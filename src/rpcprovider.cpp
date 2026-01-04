#include "rpcprovider.h"
#include "mprpcapplication.h" 
#include "zookeeperutil.h"    

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <algorithm> // for std::min

#include "google/protobuf/io/coded_stream.h"
#include "google/protobuf/io/zero_copy_stream_impl.h"
#include "rpcheader.pb.h"

// ===========================================================================
// 辅助工具：TCP 拆包核心逻辑
// ===========================================================================
// 作用：尝试从 buffer 中读取 varint32 类型的长度，但不移动 buffer 的读取指针（Peek）。
// 为什么需要 Peek？因为如果数据不够（半包），我们不能消费 buffer，要等下次数据齐了一起读。
static bool PeekVarint32(muduo::net::Buffer* buffer, uint32_t& value, size_t& varint_size) {
  const char* data = buffer->peek();
  size_t readable = buffer->readableBytes();
  
  if (readable == 0) return false;

  // Varint32 最多占用 5 个字节。我们只读目前有的数据，最多读5字节。
  // ArrayInputStream 是 Protobuf 提供的零拷贝流适配器，直接包裹 muduo 的 buffer
  google::protobuf::io::ArrayInputStream array_input(data, std::min(readable, size_t(5)));
  google::protobuf::io::CodedInputStream coded_input(&array_input);
  
  // ReadVarint32 会自动处理变长编码逻辑 (例如小于128的数占1字节)
  if (!coded_input.ReadVarint32(&value)) {
    return false; // 数据不够解析出一个完整的 varint（例如只收到了 varint 的前半部分）
  }
  
  // 记录这个 varint 到底占用了几个字节（1~5字节不等），后续用于计算偏移量
  varint_size = coded_input.CurrentPosition();
  return true;
}

// ===========================================================================
// RpcProvider 实现
// ===========================================================================

//Config配置是自己定义的一个结构体，用于存储RPC服务提供者的各种配置参数。
RpcProvider::RpcProvider(const Config& config)
    : config_(config), event_loop_(), server_(nullptr) {
  
  // 1. 配置校验：防止因参数错误导致服务启动后行为异常
  if (!ValidateConfig()) {
    LOG_CRITICAL("Invalid RPC provider configuration");
    exit(EXIT_FAILURE);
  }

  // 2. 日志级别设置：根据配置动态调整，方便调试 vs 生产环境切换
  // 日志系统已经在 MprpcApplication::Init 中初始化完成了
  // if (config_.log_level == "DEBUG") {
  //   muduo::Logger::setLogLevel(muduo::Logger::DEBUG);
  // } else if (config_.log_level == "INFO") {
  //   muduo::Logger::setLogLevel(muduo::Logger::INFO);
  // } else if (config_.log_level == "WARN") {
  //   muduo::Logger::setLogLevel(muduo::Logger::WARN);
  // } else if (config_.log_level == "ERROR") {
  //   muduo::Logger::setLogLevel(muduo::Logger::ERROR);
  // }

  LOG_INFO("RpcProvider initialized: ip={}, port={}, threads={}, max_msg_size={}", 
             config_.ip, 
             config_.port, 
             config_.thread_num, 
             config_.max_message_size);
}

RpcProvider::~RpcProvider() {
  // 取消注册 Hook（防止野指针）
  if (shutdown_hook_id_ != -1) { // 意味着 Hook 成功注册了。
    MprpcApplication::GetInstance().UnregisterShutdownHook(shutdown_hook_id_);
  }
  Shutdown(); // 析构时确保资源释放
}

// 校验逻辑：确保rpc服务启动时候所有来自配置文件的参数在合理范围内
bool RpcProvider::ValidateConfig() const {
  if (config_.port == 0) {
    LOG_ERROR("Invalid port: {}", config_.port);
    return false;
  }
  if (config_.thread_num <= 0 || config_.thread_num > 64) {
    LOG_ERROR("Invalid thread_num: {}", config_.thread_num);
    return false;
  }
  if (config_.max_connections <= 0) {
    LOG_ERROR("Invalid max_connections: {}", config_.max_connections);
    return false;
  }
  // 限制最大包大小（例如1GB），防止恶意的大包攻击导致 OOM (内存耗尽)
  if (config_.max_message_size <= 0 || config_.max_message_size > 1024*1024*1024) {
    LOG_ERROR("Invalid max_message_size: {}", config_.max_message_size);
    return false;
  }
  return true;
}


// 优雅关闭：断开连接，停止循环
void RpcProvider::Shutdown() {
  // 使用 原子的标志位 shutdown_flag_， 防止多次调用，exchange是原子操作，返回旧值
  if (shutdown_flag_.exchange(true)) {   //原子地把值改为 true
    return;
  }

  LOG_INFO("RpcProvider shutting down...");

  // -----------------------------------------------------------------------
  // 第一步：ZK 下线 (通知客户端)
  // -----------------------------------------------------------------------
  // 必须最先做！断开 ZK Session，临时节点立即消失。
  // 客户端监听到节点消失，就不会再向本节点发新请求了。
  LOG_INFO("[Shutdown Step 1] Unregistering from Zookeeper...");
  ZkClient::GetInstance().Stop();

  // -----------------------------------------------------------------------
  // 第二步：等待正在进行的请求处理完毕
  // -----------------------------------------------------------------------
  // 这是一个策略选择。
  // 简单做法：设置一个标志位，让正在运行的 HandleRpcRequest 知道要停了
  // 进阶做法：等待 pending_requests 归零 (需加超时机制，防止死等)

  LOG_INFO("[Shutdown Step 2] Waiting for pending requests...");

  // 简单倒计时等待（例如最多等 3 秒）
  int retries = 0;
  while (metrics_.pending_requests > 0 && retries < 30) {
      usleep(100000); // 睡 100ms
      retries++;
      if (retries % 10 == 0) {
          LOG_INFO("Waiting for {} pending requests...", metrics_.pending_requests);
      }
  }
  
  if (metrics_.pending_requests > 0) {
      LOG_WARN("Timeout! Forcing shutdown with {} active requests.", metrics_.pending_requests);
  } else {
      LOG_INFO("All requests finished.");
  }

  // -----------------------------------------------------------------------
  // 第三步：断开所有连接
  // -----------------------------------------------------------------------
  {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (auto& pair : connections_) {
      // 主动断开所有现有连接
      if (pair.second && pair.second->conn) {
        pair.second->conn->shutdown();
      }
    }
    connections_.clear();
  }

  // 最后退出 EventLoop
  if (event_loop_.eventHandling()) {
    event_loop_.quit();
  }

  // 打印最终统计信息
  LOG_INFO("RpcProvider shutdown complete. Stats - "
           "Total: {}, Failed: {}, Partial: {}",
           metrics_.total_requests,
           metrics_.failed_requests,
           metrics_.partial_messages);
}

// 服务注册：利用 Protobuf 反射机制构建路由表，是服务器自己将自己的支持的服务进行注册，依据是自己规定的业务.proto文件
// 这里的 Service* 是业务逻辑类（如 RaftService）的基类指针
bool RpcProvider::NotifyService(google::protobuf::Service* service) {
  //如果传入的service指针为空，则记录错误日志并返回false，表示注册失败。
  if (!service) {
    LOG_ERROR("Null service pointer");
    return false;
  }

  // 获取 Service 的描述符 (Descriptor)
  const google::protobuf::ServiceDescriptor* service_desc = service->GetDescriptor();
  std::string service_name = service_desc->name();

  // 加锁保护 map，虽然通常 NotifyService 是在单线程启动阶段调用的，但加锁更安全
  std::lock_guard<std::mutex> lock(service_mutex_);
  
  //检查服务是否已注册
  if (service_map_.find(service_name) != service_map_.end()) {
    LOG_WARN("Service already registered: {}", service_name);
    return false;
  }

  ServiceInfo service_info;
  service_info.service = service;

  // 遍历并注册该服务下的所有方法
  int method_count = service_desc->method_count();
  for (int i = 0; i < method_count; ++i) {
    const google::protobuf::MethodDescriptor* method_desc = service_desc->method(i);
    std::string method_name = method_desc->name();
    service_info.method_map[method_name] = method_desc;
    LOG_INFO("Registered method: {}.{},", service_name, method_name);
  }

  service_map_[service_name] = service_info;
  LOG_INFO("Service registered: {} with {} methods", service_name, method_count);
  return true;
}

// 初始化底层网络库（Muduo），绑定业务逻辑回调，启动事件循环。执行函数后，服务器将正式开始监听端口，等待客户端连接和请求
void RpcProvider::Run() {
  if (shutdown_flag_) {
    LOG_WARN("RpcProvider already shutdown, cannot run");
    return;
  }

  std::string ip = config_.ip;
  if (ip.empty()) {
    ip = GetLocalIP(); // 自动获取本机 IP
  }

  // 更新配置中的 IP，确保注册到 ZK 的是真实 IP
  config_.ip = ip;

  muduo::net::InetAddress address(ip, config_.port);
  
  // 创建了一个 Muduo 的 TcpServer 对象，并把它存入 server_ 中
  // 使用 unique_ptr 管理 server 生命周期
  server_ = std::make_unique<muduo::net::TcpServer>(
      &event_loop_, address, "RpcProvider");

  // 绑定回调：连接建立/断开时调用 OnConnection
  server_->setConnectionCallback(
      std::bind(&RpcProvider::OnConnection, this, std::placeholders::_1));
  
  // 绑定回调：有数据可读时调用 OnMessage
  server_->setMessageCallback(
      std::bind(&RpcProvider::OnMessage, this,
                std::placeholders::_1,
                std::placeholders::_2,
                std::placeholders::_3));

  // 设置 Reactor 线程数，实现高并发处理 (One Loop Per Thread)
  server_->setThreadNum(config_.thread_num);

  // 启动空闲连接检查定时器：每 30 秒执行一次 CheckIdleConnections，此函数实现清理长时间不活跃的“僵尸连接”
  if (config_.idle_timeout_seconds > 0) {
    event_loop_.runEvery(30.0, [this]() { CheckIdleConnections(); });
  }

  LOG_INFO("RpcProvider starting at {}:{} with {} threads", ip, config_.port, config_.thread_num);

  // 注册到 MprpcApplication 的生命周期管理
  int hook_id = MprpcApplication::GetInstance().RegisterShutdownHook([this]() {
    LOG_INFO("MprpcApplication triggered shutdown, stopping RpcProvider...");
    this->Shutdown();  // 当收到信号时，自动调用 Shutdown
  });

  // 保存 hook_id，以便析构时取消注册（可选）
  shutdown_hook_id_ = hook_id;
  
  server_->start();   // 此时端口才真正打开，可以接收 TCP 握手

  //========================================================
  //连接 ZK 并注册服务
  //========================================================
  ZkConfig zk_conf;    // ZKClient 配置结构体，用于初始化ZkClient单例
  zk_conf.host = MprpcApplication::GetInstance().GetConfig().Load("zookeeper_ip");  // 从配置文件获取 ZK 服务器地址
  zk_conf.host += ":" + MprpcApplication::GetInstance().GetConfig().Load("zookeeper_port");  // 追加端口
  zk_conf.session_timeout_ms = 30000;
  zk_conf.root_path = "/mprpc"; // Zk 服务端的默认rpc根路径 

  // 启动 rpc 服务端的 zk 客户端单例
  ZkClient& zk_cli = ZkClient::GetInstance();
  zk_cli.Init(zk_conf);
  
  if (zk_cli.Start()) {
      // 遍历所有已加载的 Service，注册到 ZK
      // 路径格式: /mprpc/ServiceName/ip:port
      for (auto& sp : service_map_) {
          std::string service_name = sp.first;
          std::string service_path = service_name;
          std::string service_addr = ip + ":" + std::to_string(config_.port); // rpc服务端IP:Port = 办理服务的IP:Port
          
          // 注册服务（创建临时节点）
          if (zk_cli.RegisterService(service_name, service_addr)) { // 此函数最终创建出/mprpc/ServiceName/ip:port临时节点
              LOG_INFO("Successfully registered service to ZK: {} -> {}", service_name, service_addr);
          } else {
              LOG_ERROR("Failed to register service to ZK: {}", service_name);
          }
      }
  } else {
      LOG_ERROR("Failed to start ZkClient, services will not be discoverable!");
  }

  LOG_INFO("RpcProvider enter event loop...");
  event_loop_.loop(); // 进入事件循环，阻塞在此，通过 `epoll_wait` 等待网络事件发生
}

// 连接事件回调
void RpcProvider::OnConnection(const muduo::net::TcpConnectionPtr& conn) {
  if (conn->connected()) {
    // 1. 连接限制检查
    if (metrics_.active_connections >= config_.max_connections) {
      LOG_WARN("Max connections reached, rejecting: {}", conn->peerAddress().toIpPort());
      conn->shutdown();
      return;
    }

    // 2. 创建连接上下文，记录活跃时间
    auto ctx = std::make_shared<ConnectionContext>();
    ctx->conn = conn;
    ctx->last_active_time = muduo::Timestamp::now(); // 初始化时间戳

    {
      std::lock_guard<std::mutex> lock(conn_mutex_);
      connections_[conn->name()] = ctx;
    }

    metrics_.active_connections++;
    LOG_INFO("Connection established: {} , active: {}", conn->peerAddress().toIpPort(), metrics_.active_connections);
  } else {
    // 连接断开，清理上下文
    RemoveConnection(conn->name());
    metrics_.active_connections--;
    LOG_INFO("Connection closed: {} , active: {}", conn->peerAddress().toIpPort(), metrics_.active_connections);
  }
}

// 核心网络读回调：处理 TCP 粘包和拆包
void RpcProvider::OnMessage(const muduo::net::TcpConnectionPtr& conn,
                            muduo::net::Buffer* buffer,
                            muduo::Timestamp timestamp) {
  // 1. 更新活跃时间（用于心跳保活）
  {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = connections_.find(conn->name());
    if (it != connections_.end()) {
      it->second->last_active_time = timestamp;
      it->second->pending_requests++;
    }
  }

  // 2. 循环解析：处理 Buffer 中可能存在的多个包 (TCP 粘包)
  // 如果收到半个包，循环会因 TryParseMessage 返回 false 而终止
  while (true) {
    uint32_t header_size = 0;
    std::string service_name, method_name, args_str;

    // 尝试解析一个完整的消息
    // 返回 true 表示成功解析了一个包，buffer 指针已后移
    // 返回 false 表示数据不够（半包），buffer 指针未动，等待下次数据到来
    bool parsed = TryParseMessage(buffer, header_size, 
                                  service_name, method_name, args_str);
    
    if (!parsed) {
      // 半包：数据不够，退出循环，等待 TCP 继续传输
      metrics_.partial_messages++;
      // Debug log, 生产环境可降低级别
      LOG_DEBUG("Partial message received, waiting for more data. "
                "Buffer size: {}", buffer->readableBytes());
      break; 
    }

    // 3. 完整包解析成功，开始业务处理
    metrics_.total_requests++;

    // 正在处理的请求计数 +1
    metrics_.pending_requests++;
    
    try {
      HandleRpcRequest(conn, service_name, method_name, args_str);
    } catch (const std::exception& e) {
      LOG_ERROR("Exception handling RPC request: {}", e.what());
      SendErrorResponse(conn, RPC_INTERNAL_ERROR, e.what());
      metrics_.failed_requests++;
    }
  }

  // 请求处理完成计数
  {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = connections_.find(conn->name());
    if (it != connections_.end()) {
      it->second->pending_requests--;
    }
  }
}

// 协议解析器：状态机模式（改进版：全 Peek 策略）
// 协议格式：[Varint32: header_size] + [RpcHeader] + [Args]
bool RpcProvider::TryParseMessage(muduo::net::Buffer* buffer,
                                  uint32_t& header_size,
                                  std::string& service_name,
                                  std::string& method_name,
                                  std::string& args_str) {
  // ==========================================================
  // 阶段 1: 偷看 (Peek) 头部长度
  // ==========================================================
  size_t varint_size = 0;
  // 这里使用辅助函数 peek，不移动 buffer 指针
  if (!PeekVarint32(buffer, header_size, varint_size)) {
    // 数据太少，连长度头都没收齐，等待下次
    return false;
  }

  // 校验 header 长度合法性
  if (header_size == 0 || header_size > config_.max_message_size) {
    LOG_ERROR("Invalid header size: {}", header_size);
    // 这种情况下，数据已经损坏，必须丢弃或断开。
    // 为了防止死循环，我们 retrieve 掉这个坏的 varint，并抛出异常让外层断开连接
    buffer->retrieve(varint_size); 
    throw std::runtime_error("Invalid header size");
  }

  // ==========================================================
  // 阶段 2: 偷看 (Peek) RpcHeader
  // ==========================================================
  
  // 计算读取 Header 所需的总字节数 (长度头 + Header体)
  size_t total_header_len = varint_size + header_size;
  
  // 如果 Buffer 里的数据还不够 Header 的长度，直接返回，什么都不动
  if (buffer->readableBytes() < total_header_len) {
    return false; // Header 还没收齐，等待下次
  }

  // 此时数据够了，我们手动从 Buffer 中“偷”出 Header 的数据进行解析
  // buffer->peek() 是起始位置，偏移 varint_size 就是 Header 的开始
  const char* header_start = buffer->peek() + varint_size;
  
  // 使用收到的二进制数据流拷贝构造临时的 string ，用于反序列化
  std::string rpc_header_str(header_start, header_size);
  
  RPC::RpcHeader rpc_header;
  if (!rpc_header.ParseFromString(rpc_header_str)) {
    LOG_ERROR("Failed to parse RPC header");
    // 协议错乱，消费掉已读取的部分，抛出异常断开连接
    buffer->retrieve(total_header_len);
    throw std::runtime_error("Invalid RPC header");
  }

  service_name = rpc_header.service_name();
  method_name = rpc_header.method_name();
  uint32_t args_size = rpc_header.args_size();

  // 校验 args 长度
  if (args_size > config_.max_message_size) {
    LOG_ERROR("Args size too large: {}", args_size);
    // 长度异常，消费掉已读取的部分，抛出异常断开连接
    buffer->retrieve(total_header_len); 
    throw std::runtime_error("Message too large");
  }

  // ==========================================================
  // 阶段 3: 检查整体完整性 (Check Total Size)
  // ==========================================================
  
  // 整个包的总长度 = 长度头(varint) + Header数据 + Args数据
  size_t total_package_len = varint_size + header_size + args_size;

  // 关键判断：只有当 Buffer 数据 >= 整个包长度时，才开始消费
  if (buffer->readableBytes() < total_package_len) {
    // Args 还没收齐，等待下次。此时 Buffer 指针完全没动！
    // 这解决了旧代码“读了Header发现Args不够，导致下次Header丢失”的Bug
    return false; 
  }

  // ==========================================================
  // 阶段 4: 正式消费数据 (Retrieve)
  // ==========================================================
  
  // 1. 消费掉 [长度头 + Header]
  buffer->retrieve(varint_size + header_size);
  
  // 2. 消费并读取 [Args]
  // 此时 Buffer 的 readerIndex 已经指向了 Args 的开头
  args_str = buffer->retrieveAsString(args_size);

  LOG_DEBUG("Parsed complete message: {} . {}, args size: {}", service_name, method_name, args_size);

  return true;
}

// 业务分发
void RpcProvider::HandleRpcRequest(const muduo::net::TcpConnectionPtr& conn,
                                   const std::string& service_name,
                                   const std::string& method_name,
                                   const std::string& args_str) {
  // 1. 查找服务和方法
  google::protobuf::Service* service = nullptr;
  const google::protobuf::MethodDescriptor* method = nullptr;

  {
    std::lock_guard<std::mutex> lock(service_mutex_);
    
    auto service_it = service_map_.find(service_name);
    if (service_it == service_map_.end()) {
      LOG_WARN("Service not found: {}", service_name);
      SendErrorResponse(conn, RPC_SERVICE_NOT_FOUND, "Service not found: " + service_name);
      return;
    }

    auto method_it = service_it->second.method_map.find(method_name);
    if (method_it == service_it->second.method_map.end()) {
      LOG_WARN("Method not found: {} . {}", service_name, method_name);
      SendErrorResponse(conn, RPC_METHOD_NOT_FOUND, "Method not found: " + method_name);
      return;
    }

    service = service_it->second.service;
    method = method_it->second;
  }

  // 2. 创建请求和响应对象 (Protobuf 反射)
  std::unique_ptr<google::protobuf::Message> request(
      service->GetRequestPrototype(method).New());
  std::unique_ptr<google::protobuf::Message> response(
      service->GetResponsePrototype(method).New());

  // 3. 反序列化请求参数
  if (!request->ParseFromString(args_str)) {
    LOG_ERROR("Failed to parse request arguments");
    SendErrorResponse(conn, RPC_INVALID_REQUEST, "Failed to parse request arguments");
    return;
  }

  // 4. 绑定回调闭包
  // 注意：response.get() 传给回调，response.release() 放弃所有权，防止被提前析构
  auto response_ptr = response.get();
  
  //  绑定回调闭包 (Closure)
  // 相当于构造一个回调函数：当业务做完后，请调用 this->SendRpcResponse
  google::protobuf::Closure* done = google::protobuf::NewCallback(
      this, &RpcProvider::SendRpcResponse, conn, response_ptr);

  // 5. 执行业务逻辑
  // 这一步会跳转到 RaftService 的实现代码中
  // done->Run() 会在业务逻辑处理完毕后被调用
  service->CallMethod(method, nullptr, request.get(), response_ptr, done);
  
  response.release(); // 所有权转移给 Closure，SendRpcResponse 负责 delete
}

// 发送响应：添加长度头
void RpcProvider::SendRpcResponse(muduo::net::TcpConnectionPtr conn,
                                  google::protobuf::Message* response) {
  // 接管 response 所有权，确保函数结束时自动 delete，防止内存泄漏
  std::unique_ptr<google::protobuf::Message> response_guard(response);

  std::string response_str;
  if (!response->SerializeToString(&response_str)) {
    LOG_ERROR("Failed to serialize response");
    SendErrorResponse(conn, RPC_INTERNAL_ERROR, "Failed to serialize response");
    return;
  }

  // 构造带长度头的帧：[varint32: size] + [data]
  // 客户端收到后，也必须先读 varint 长度，再读 data
  std::string frame;
  {
    google::protobuf::io::StringOutputStream string_output(&frame);
    google::protobuf::io::CodedOutputStream coded_output(&string_output);
    coded_output.WriteVarint32(response_str.size()); // 写入长度
    // CodedOutputStream 析构时会 flush 到 frame
  }
  frame.append(response_str); // 追加数据data部分

  conn->send(frame); // 发送的是frame

  metrics_.pending_requests--; // 响应发送完毕，正在处理的请求结束，计数 -1
  LOG_DEBUG("Response sent: {} bytes", response_str.size());
}

// 发送错误响应（用于协议错误或系统错误）
void RpcProvider::SendErrorResponse(const muduo::net::TcpConnectionPtr& conn,
                                    int error_code,
                                    const std::string& error_msg) {
  std::ostringstream oss;
  // 简单的错误协议：ERROR:code:msg
  oss << "ERROR:" << error_code << ":" << error_msg;
  std::string error_str = oss.str();
  
  // 同样需要加帧头
  std::string frame;
  {
    google::protobuf::io::StringOutputStream string_output(&frame);
    google::protobuf::io::CodedOutputStream coded_output(&string_output);
    coded_output.WriteVarint32(error_str.size());
  }
  frame.append(error_str);
  
  conn->send(frame);

  // 错误响应发送完毕，请求结束，计数 -1
  metrics_.pending_requests--;
  LOG_WARN("Error response: code={}, msg={}", error_code, error_msg);
}

// 检查空闲连接
void RpcProvider::CheckIdleConnections() {
  if (shutdown_flag_) return;

  auto now = muduo::Timestamp::now();
  std::vector<std::string> idle_conns;

  {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    for (const auto& pair : connections_) {
      // 只有当前没有正在处理的请求时，才计算空闲时间
      if (pair.second->pending_requests == 0) {
        double idle_seconds = muduo::timeDifference(
            now, pair.second->last_active_time);
        
        if (idle_seconds > config_.idle_timeout_seconds) {
          idle_conns.push_back(pair.first);
        }
      }
    }
  }

  // 执行断开操作
  for (const auto& conn_name : idle_conns) {
    std::lock_guard<std::mutex> lock(conn_mutex_);
    auto it = connections_.find(conn_name);
    if (it != connections_.end() && it->second->conn) {
      LOG_INFO("Closing idle connection: {}",
               it->second->conn->peerAddress().toIpPort());
      it->second->conn->shutdown();
    }
  }
}

void RpcProvider::RemoveConnection(const std::string& conn_name) {
  std::lock_guard<std::mutex> lock(conn_mutex_);
  connections_.erase(conn_name);
}

// 获取本机 IP（辅助函数）
std::string RpcProvider::GetLocalIP() const {
  char hostname[256];
  if (gethostname(hostname, sizeof(hostname)) != 0) {
    LOG_ERROR("gethostname failed");
    return "127.0.0.1";
  }

  struct hostent* host = gethostbyname(hostname);
  if (!host || !host->h_addr_list[0]) {
    LOG_ERROR("gethostbyname failed");
    return "127.0.0.1";
  }

  // 优先寻找非回环地址 (127.x.x.x 以外的地址)
  for (int i = 0; host->h_addr_list[i]; ++i) {
    char* ip = inet_ntoa(*(struct in_addr*)(host->h_addr_list[i]));
    if (ip && strncmp(ip, "127.", 4) != 0) {
      LOG_INFO("Detected local IP: {}", ip);
      return std::string(ip);
    }
  }

  // 只有回环地址时，返回回环地址
  char* ip = inet_ntoa(*(struct in_addr*)(host->h_addr_list[0]));
  return std::string(ip ? ip : "127.0.0.1");
}