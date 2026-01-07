/**
 * @file mprpcapplication.cpp
 * @brief RPC 框架应用程序管理类实现
 */

#include "mprpcapplication.h"

#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
// spdlog 相关头文件
#include <spdlog/sinks/stdout_color_sinks.h> // 控制台输出
#include <spdlog/sinks/basic_file_sink.h>    // 基础文件输出
#include <spdlog/sinks/rotating_file_sink.h> // 滚动文件输出 (推荐)
#include <spdlog/async.h>                    // 异步日志支持

// ============================================================================
// 静态成员变量初始化
// ============================================================================

std::atomic<bool> MprpcApplication::s_initialized_(false); // 原子状态位，防止 `Init()` 被多线程重复调用。
std::atomic<bool> MprpcApplication::s_shutting_down_(false); // 原子状态位，防止 `Shutdown()` 被重复触发

// ============================================================================
// 构造和析构
// ============================================================================

/**
 * @brief 私有构造函数（单例模式）
 * @details 初始化成员变量，不执行实际的框架初始化
 */
MprpcApplication::MprpcApplication() 
    : config_(MprpcConfig::GetInstance()),  // 引用全局配置单例
      args_() {
  // 构造函数留空，真正的初始化在 Init() 中完成
}

/**
 * @brief 析构函数
 * @details 触发优雅关闭，清理资源
 */
MprpcApplication::~MprpcApplication() {
  // ========== 完成所有业务清理 ==========
  if (!s_shutting_down_) {
    std::cout << "\n[MprpcApplication] 开始析构..." << std::endl;
    Shutdown();
  }

  // 不要在析构里再调用 spdlog::shutdown()，除非你能保证顺序
  // spdlog::shutdown();
}

// ============================================================================
// 单例访问
// ============================================================================

/**
 * @brief 获取全局单例实例（线程安全，C++11 保证）
 * @return 应用程序单例引用
 */
MprpcApplication& MprpcApplication::GetInstance() {
  // C++11 保证静态局部变量的线程安全初始化
  static MprpcApplication instance;
  return instance;
}

// ============================================================================
// 框架初始化
// ============================================================================

/**
 * @brief 初始化 RPC 框架
 * @param argc 命令行参数个数
 * @param argv 命令行参数数组
 * @details
 * 执行步骤：
 * 1. 检查是否重复初始化
 * 2. 解析命令行参数
 * 3. 处理 --help 参数
 * 4. 加载配置文件
 * 5. 加载环境变量
 * 6. 初始化日志系统
 * 7. 注册信号处理器
 * 8. 设置初始化标志
 */
void MprpcApplication::Init(int argc, char* argv[]) {
  // 1. 防止重复初始化
  bool expected = false;
  if (!s_initialized_.compare_exchange_strong(expected, true)) { // 如果 s_initialized_ 当前等于 expected，则将其设置为 true 并返回 true
    std::cerr << "[MprpcApplication] Warning: Framework already initialized!" << std::endl;
    return;
  }

  auto& app = GetInstance(); // 获取application单例引用

  std::cout << "[MprpcApplication] Initializing " << FRAMEWORK_NAME 
            << " v" << FRAMEWORK_VERSION << "..." << std::endl;

  // 2. 解析命令行参数argv
  // ParseCommandLine 函数把argv的参数包装成 CommandLineArgs 结构体放入 args_ 成员变量中
  app.args_ = app.ParseCommandLine(argc, argv);

  // 3. 如果用户传了 --help，打印帮助后退出
  if (app.args_.help) {
    PrintHelp();
    exit(EXIT_SUCCESS);
  }

  // 4. 加载配置文件，命令行参数结构体是自己定义的
  if (!app.args_.config_file.empty()) {
    std::cout << "[MprpcApplication] Loading config file: " 
              << app.args_.config_file << std::endl;
    app.config_.LoadConfigFile(app.args_.config_file.c_str()); // 使用MprpcConfig加载配置文件
  } else {
    // 如果没有指定配置文件，尝试加载默认路径
    const char* default_configs[] = {
      "rpc.ini",           // 当前目录
      "./config/rpc.ini",  // config 子目录
      "/etc/mprpc/rpc.ini" // 系统目录
    };

    bool loaded = false;
    // 尝试加载第一个存在的默认配置文件
    for (const char* path : default_configs) {
      std::ifstream test_file(path);
      if (test_file.good()) {
        std::cout << "[MprpcApplication] Loading default config: " << path << std::endl;
        app.config_.LoadConfigFile(path);
        loaded = true;  // 成功加载
        break;
      }
    }

    if (!loaded) {
      std::cerr << "[MprpcApplication] Warning: No config file found, using defaults" << std::endl;
    }
  }

  // 5. 加载环境变量（如果有就用它覆盖配置文件）
  std::cout << "[MprpcApplication] Loading environment variables (RPC_* prefix)..." << std::endl;
  app.config_.LoadEnvVariables("RPC_");

  // 6. 初始化日志系统
  std::string log_file = app.args_.log_file;
  if (log_file.empty()) {
    // 从配置文件读取日志路径
    log_file = app.config_.Load("log.file");
  }

  std::string log_level = app.args_.log_level;
  if (log_level.empty()) {
    log_level = app.config_.Load("log.level");
    if (log_level.empty()) {
      log_level = "INFO";  // 默认日志级别
    }
  }

  app.InitLoggingAsync(log_file, log_level);  // 正式初始化日志系统

  // 7. 注册信号处理器（捕获 Ctrl+C 等信号）
  app.RegisterSignalHandlers();

  LOG_INFO("Framework initialized successfully!");
  LOG_INFO("=================================================");
  // std::cout << "[MprpcApplication] Framework initialized successfully!" << std::endl;
  // std::cout << "=================================================" << std::endl;
}

/**
 * @brief 检查框架是否已初始化
 */
bool MprpcApplication::IsInitialized() {
  return s_initialized_.load();
}

// ============================================================================
// 配置访问
// ============================================================================

/**
 * @brief 获取配置管理器
 * @return 配置管理器引用
 */
MprpcConfig& MprpcApplication::GetConfig() {
  if (!IsInitialized()) {
    // 严重错误，日志还没初始化，只能用 cerr
    std::cerr << "[MprpcApplication] Fatal Error: Framework not initialized! "
              << "Call Init() first." << std::endl;
    exit(EXIT_FAILURE);
  }
  return config_;
}

/**
 * @brief 获取命令行参数
 */
const CommandLineArgs& MprpcApplication::GetArgs() const {
  return args_;
}

// ============================================================================
// 生命周期管理
// ============================================================================

/**
 * @brief 注册关闭钩子
 * @param hook 关闭回调函数
 * @return 钩子 ID
 * @details
 * 其他模块可以在自己的逻辑里注册钩子函数
 * 钩子会在 Shutdown() 时按**注册顺序逆序**执行
 * 例如：注册顺序 A->B->C，执行顺序 C->B->A
 */
int MprpcApplication::RegisterShutdownHook(std::function<void()> hook) {// 传入的参数是一个函数对象（可以是 lambda、函数指针等）
  std::lock_guard<std::mutex> lock(shutdown_hooks_mutex_); // 加锁，保护 shutdown_hooks_ 容器
  
  int hook_id = next_hook_id_++;
  shutdown_hooks_.emplace_back(hook_id, hook); // 存入 ID — 函数对象
  
  LOG_INFO("Registered shutdown hook #{}", hook_id);
  // std::cout << "[MprpcApplication] Registered shutdown hook #" << hook_id << std::endl;
  return hook_id;
}

/**
 * @brief 取消注册关闭钩子
 * @param hook_id 钩子 ID
 */
void MprpcApplication::UnregisterShutdownHook(int hook_id) {
  std::lock_guard<std::mutex> lock(shutdown_hooks_mutex_);
  
  auto it = std::find_if(shutdown_hooks_.begin(), shutdown_hooks_.end(),
                        [hook_id](const auto& pair) { return pair.first == hook_id; });
  
  if (it != shutdown_hooks_.end()) {
    shutdown_hooks_.erase(it);
    LOG_INFO("Unregistered shutdown hook #{}", hook_id);
    // std::cout << "[MprpcApplication] Unregistered shutdown hook #" << hook_id << std::endl;
  }
}

/**
 * @brief 触发优雅关闭
 * @details
 * 执行步骤：
 * 1. 设置关闭标志，防止重复关闭
 * 2. 检查日志系统是否可用
 * 3. 逆序调用所有关闭钩子
 */
void MprpcApplication::Shutdown() {
  // 1. 使用原子操作防止重复关闭
  bool expected = false;
  // 如果 s_shutting_down_ 当前等于 expected，则将其设置为 true 并返回 true，否则直接返回
  if (!s_shutting_down_.compare_exchange_strong(expected, true)) {
    return;  // 已经在关闭中
  }

  // 2. 检查日志系统是否可用
  // 在关闭过程中，我们不能假设 spdlog 一定活着
  bool logger_alive = (spdlog::default_logger() != nullptr);

  // 辅助 lambda：安全打印日志：如果 logger 挂了，就用 std::cout/cerr 打印
  auto safe_log = [&](const std::string& msg, bool is_error = false) {
      if (logger_alive) {
          if (is_error) LOG_ERROR("{}", msg);
          else LOG_INFO("{}", msg);
      } else {
          // 如果 logger 挂了，降级到 std::cout/cerr
          if (is_error) std::cerr << "[MprpcApplication] Error: " << msg << std::endl;
          else std::cout << "[MprpcApplication] " << msg << std::endl;
      }
  };

  std::cout << "检查完毕日志是否可用：" << logger_alive << std::endl;
  safe_log("Shutting down gracefully..."); 

  // 3. 逆序调用关闭钩子（类似于栈的 LIFO 顺序）
  {
    std::lock_guard<std::mutex> lock(shutdown_hooks_mutex_);
    safe_log("开始执行 shutdown hooks... ，hook数量：" + std::to_string(shutdown_hooks_.size()), false);
    for (auto it = shutdown_hooks_.rbegin(); it != shutdown_hooks_.rend(); ++it) {
      try {
        safe_log("Executing shutdown hook #" + std::to_string(it->first), false);
        it->second(); // 执行回调
      } catch (const std::exception& e) {
        safe_log("Exception in shutdown hook #" + std::to_string(it->first) + ": " + e.what(), true);
      }
    }
  }

  safe_log("Shutdown complete.");
}

/**
 * @brief 检查是否正在关闭
 */
bool MprpcApplication::IsShuttingDown() const {
  return s_shutting_down_.load();
}

// ============================================================================
// 辅助功能
// ============================================================================

/**
 * @brief 打印框架版本信息
 */
void MprpcApplication::PrintVersion() {
  std::cout << FRAMEWORK_NAME << " version " << FRAMEWORK_VERSION << std::endl;
  std::cout << "Build time: " << __DATE__ << " " << __TIME__ << std::endl;
}

/**
 * @brief 打印使用帮助
 */
void MprpcApplication::PrintHelp() {
  std::cout << "\n";
  std::cout << "=================================================" << std::endl;
  std::cout << "  " << FRAMEWORK_NAME << " v" << FRAMEWORK_VERSION << std::endl;
  std::cout << "=================================================" << std::endl;
  std::cout << "\nUsage: <program> [OPTIONS]\n" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --config=<file>       指定配置文件路径 (默认: rpc.ini)" << std::endl;
  std::cout << "  --log=<file>          指定日志文件路径" << std::endl;
  std::cout << "  --log_level=<level>   设置日志级别 (DEBUG/INFO/WARN/ERROR)" << std::endl;
  std::cout << "  --daemon              以守护进程模式运行" << std::endl;
  std::cout << "  --help                显示此帮助信息" << std::endl;
  std::cout << "\n环境变量支持 (优先级高于配置文件):" << std::endl;
  std::cout << "  RPC_RPCSERVER_IP      服务端 IP 地址" << std::endl;
  std::cout << "  RPC_RPCSERVER_PORT    服务端端口" << std::endl;
  std::cout << "  RPC_*                 其他配置项 (格式: RPC_SECTION_KEY)" << std::endl;
  std::cout << "\n示例:" << std::endl;
  std::cout << "  ./rpc_server --config=config/server.ini" << std::endl;
  std::cout << "  ./rpc_server --log=/var/log/rpc.log --log_level=DEBUG" << std::endl;
  std::cout << "  RPC_RPCSERVER_PORT=9090 ./rpc_server" << std::endl;
  std::cout << "\n=================================================" << std::endl;
}

/**
 * @brief 打印所有配置项（调试用）
 */
void MprpcApplication::PrintConfig() const {
  LOG_INFO("=================================================");
  LOG_INFO(" Current Configuration");
  LOG_INFO("=================================================");
  
  // spdlog 无法直接打印 config 对象，需要一个个取
  LOG_INFO("RpcServer.ip      = {}", config_.Load("RpcServer.ip"));
  LOG_INFO("RpcServer.port    = {}", config_.Load("RpcServer.port"));
  LOG_INFO("log.level         = {}", config_.Load("log.level"));
  LOG_INFO("log.file          = {}", config_.Load("log.file"));
  
  LOG_INFO("=================================================");
}

// ============================================================================
// 内部方法
// ============================================================================

/**
 * @brief 解析命令行参数
 * @param argc 参数个数
 * @param argv 参数数组
 * @return 解析结果
 * @details
 * 支持的格式：
 * - --key=value
 * - --key value
 * - --flag (布尔标志)
 */
CommandLineArgs MprpcApplication::ParseCommandLine(int argc, char* argv[]) {
  CommandLineArgs args;

  for (int i = 1; i < argc; ++i) {
    std::string arg = argv[i];

    // 检查是否是选项（以 -- 开头）
    if (arg.find("--") != 0) {
      std::cerr << "[MprpcApplication] Warning: Ignoring invalid argument: " << arg << std::endl;
      continue;
    }

    // 去掉 "--" 前缀
    arg = arg.substr(2);

    // 查找 '=' 分隔符
    size_t eq_pos = arg.find('=');
    
    if (eq_pos != std::string::npos) {
      // 格式: --key=value
      std::string key = arg.substr(0, eq_pos);
      std::string value = arg.substr(eq_pos + 1);

      // 处理预定义的参数
      if (key == "config") {
        args.config_file = value;
      } else if (key == "log") {
        args.log_file = value;
      } else if (key == "log_level") {
        args.log_level = value;
      } else {
        // 存入自定义参数
        args.custom_args[key] = value;
      }
    } else {
      // 格式: --flag 或 --key value
      
      // 处理布尔标志
      if (arg == "help") {
        args.help = true;
      } else if (arg == "daemon") {
        args.daemon_mode = true;
      } 
      // 尝试读取下一个参数作为值
      else if (i + 1 < argc && argv[i + 1][0] != '-') {
        std::string value = argv[i + 1];
        i++;  // 跳过下一个参数

        if (arg == "config") {
          args.config_file = value;
        } else if (arg == "log") {
          args.log_file = value;
        } else if (arg == "log_level") {
          args.log_level = value;
        } else {
          args.custom_args[arg] = value;
        }
      } else {
        // 无值的自定义标志
        args.custom_args[arg] = "true";
      }
    }
  }

  return args;
}

/**
 * @brief 初始化日志系统
 * @param log_file 日志文件路径（空则输出到 stdout）
 * @param log_level 日志级别
 * @details
 * 集成 spdlog专业日志库
 */
void MprpcApplication::InitLogging(const std::string& log_file, const std::string& log_level) {
  std::cout << "[MprpcApplication] Initializing logging system..." << std::endl;
  std::cout << "  Log File:  " << (log_file.empty() ? "stdout" : log_file) << std::endl;
  std::cout << "  Log Level: " << log_level << std::endl;

  // 集成真正的日志库
  try {
    // ========== 如果指定了日志文件，先创建目录 ==========
    if (!log_file.empty()) {
      // 提取目录路径
      size_t last_slash = log_file.find_last_of("/\\");
      if (last_slash != std::string::npos) {
        std::string dir = log_file.substr(0, last_slash);
        // 创建目录（忽略错误，因为可能已经存在）
        system(("mkdir -p " + dir).c_str());
      }
    }

    // ========== 1. 创建 sinks（日志输出目标） ==========
    std::vector<spdlog::sink_ptr> sinks;

    // Sink 1: 控制台输出（带颜色）
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::err);  // 控制台只打印错误信息
    // 自定义控制台输出格式：[时间] [级别] [线程ID] 消息
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    sinks.push_back(console_sink);

    // Sink 2: 文件输出（如果指定了文件路径）
    if (!log_file.empty()) {
      // 使用滚动文件日志：单个文件最大 10MB，最多保留 3 个文件
      // 例如：rpc.log, rpc.log.1, rpc.log.2
      constexpr size_t max_file_size = 10 * 1024 * 1024;  // 10 MB
      constexpr size_t max_files = 3;
      
      auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          log_file, max_file_size, max_files);
      
      file_sink->set_level(spdlog::level::trace);  // 文件记录所有级别
      // 文件输出格式：更详细，包含文件名和行号
      file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%#] %v");
      sinks.push_back(file_sink);
    }

    // ========== 2. 创建全局 logger ==========
    // 使用多线程安全的 logger（_mt 后缀）
    auto logger = std::make_shared<spdlog::logger>(
        "rpc_logger",  // logger 名称
        sinks.begin(), 
        sinks.end()
    );

    // ========== 3. 设置日志级别 ==========
    spdlog::level::level_enum level = spdlog::level::info;  // 默认 INFO
    std::string level_upper = log_level;
    std::transform(level_upper.begin(), level_upper.end(), 
                   level_upper.begin(), ::toupper);
    
    if (level_upper == "TRACE") {
      level = spdlog::level::trace;
    } else if (level_upper == "DEBUG") {
      level = spdlog::level::debug;
    } else if (level_upper == "INFO") {
      level = spdlog::level::info;
    } else if (level_upper == "WARN" || level_upper == "WARNING") {
      level = spdlog::level::warn;
    } else if (level_upper == "ERROR") {
      level = spdlog::level::err;
    } else if (level_upper == "CRITICAL") {
      level = spdlog::level::critical;
    } else {
      std::cerr << "[MprpcApplication] Warning: Invalid log level '" 
                << log_level << "', using INFO" << std::endl;
    }
    
    logger->set_level(level);
    
    // ========== 4. 设置为全局默认 logger ==========
    // 这是关键！设置后，所有 LOG_INFO() 等宏都会使用这个 logger
    spdlog::set_default_logger(logger);
    
    // ========== 5. 其他配置 ==========
    // 立即刷新日志（确保不丢失）
    // 生产环境可以改为按时间刷新以提高性能
    logger->flush_on(spdlog::level::trace);
    
    // 设置全局刷新间隔（每 3 秒自动刷新一次）
    spdlog::flush_every(std::chrono::seconds(3));

    // ========== 6. 测试日志 ==========
    LOG_INFO("========================================");
    LOG_INFO("spdlog initialized successfully!");
    LOG_INFO("  Version: {}.{}.{}", SPDLOG_VER_MAJOR, SPDLOG_VER_MINOR, SPDLOG_VER_PATCH);
    LOG_INFO("  Log Level: {}", log_level);
    LOG_INFO("  Output: {}", log_file.empty() ? "Console only" : "Console + File");
    LOG_INFO("========================================");

  } catch (const spdlog::spdlog_ex& ex) {
    std::cerr << "[MprpcApplication] Fatal: spdlog initialization failed: " 
              << ex.what() << std::endl;
    exit(EXIT_FAILURE);
  }
}

/**
 * @brief 初始化异步日志系统（可选，适合高并发场景）
 * @details
 * 异步日志优势：
 * - 不阻塞业务线程（日志写入在后台线程完成）
 * - 更高的吞吐量（适合日志量大的场景）
 * 
 * 使用方法：
 * 在 InitLogging 中调用此函数替代同步版本
 */
void MprpcApplication::InitLoggingAsync(const std::string& log_file, 
                                       const std::string& log_level) {
  std::cout << "[MprpcApplication] Initializing async logging system..." << std::endl;

  try {
    // ========== 1. 初始化异步日志线程池 ==========
    // 参数：队列大小 32768，后台线程数 3
    spdlog::init_thread_pool(32768, 3);

    // ========== 2. 创建异步 sinks ==========
    std::vector<spdlog::sink_ptr> sinks;

    // 控制台输出（带颜色）
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::err); // 控制台只打印错误信息
    // 自定义控制台输出格式：[时间] [级别] [线程ID] 消息
    console_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] %v");
    sinks.push_back(console_sink);

    // 文件输出
    if (!log_file.empty()) {
      // 使用滚动文件日志：单个文件最大 10MB，最多保留 3 个文件
      // 例如：rpc.log, rpc.log.1, rpc.log.2
      constexpr size_t max_file_size = 10 * 1024 * 1024;
      constexpr size_t max_files = 3;
      
      auto file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
          log_file, max_file_size, max_files);
      file_sink->set_level(spdlog::level::trace); // 文件记录所有级别
      file_sink->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%^%l%$] [%t] [%s:%#] %v");
      sinks.push_back(file_sink);
    }

    // ========== 3. 创建异步 logger ==========
    auto logger = std::make_shared<spdlog::async_logger>(
        "rpc_async_logger",
        sinks.begin(),
        sinks.end(),
        spdlog::thread_pool(),  // 使用上面初始化的线程池
        spdlog::async_overflow_policy::block  // 队列满时阻塞（防止丢日志）
    );

    // ========== 4. 设置日志级别 ==========
    spdlog::level::level_enum level = spdlog::level::info;
    std::string level_upper = log_level;
    std::transform(level_upper.begin(), level_upper.end(), 
                   level_upper.begin(), ::toupper);
    
    if (level_upper == "TRACE") level = spdlog::level::trace;
    else if (level_upper == "DEBUG") level = spdlog::level::debug;
    else if (level_upper == "INFO") level = spdlog::level::info;
    else if (level_upper == "WARN") level = spdlog::level::warn;
    else if (level_upper == "ERROR") level = spdlog::level::err;
    else if (level_upper == "CRITICAL") level = spdlog::level::critical;
    
    logger->set_level(level);

    // ========== 5. 设置为全局默认 logger ==========
    // 这是关键！设置后，所有 LOG_INFO() 等宏都会使用这个 logger
    spdlog::set_default_logger(logger);
    
    // 异步日志建议按时间刷新（不需要每条都立即刷新）
    spdlog::flush_every(std::chrono::seconds(3));

    LOG_INFO("========================================");
    LOG_INFO("Async spdlog initialized successfully!");
    LOG_INFO("  Mode: Async (Non-blocking)");
    LOG_INFO("  Log Level: {}", log_level);
    LOG_INFO("========================================");

  } catch (const spdlog::spdlog_ex& ex) {
    std::cerr << "[MprpcApplication] Fatal: Async spdlog initialization failed: " 
              << ex.what() << std::endl;
    exit(EXIT_FAILURE);
  }
}

/**
 * @brief 注册信号处理器
 * @details
 * 捕获以下信号：
 * - SIGINT (Ctrl+C)
 * - SIGTERM (kill 命令)
 * - SIGQUIT (Ctrl+\)
 * 
 * 收到信号后，触发 Shutdown() 优雅关闭
 */
void MprpcApplication::RegisterSignalHandlers() {
  LOG_INFO("Registering signal handlers...");
  // std::cout << "[MprpcApplication] Registering signal handlers..." << std::endl;

  // 设置信号处理函数
    // 这是系统级别的调用，触发后就会执行 SignalHandler 函数
  signal(SIGINT, SignalHandler);   // Ctrl+C
  signal(SIGTERM, SignalHandler);  // kill 命令
  signal(SIGQUIT, SignalHandler);  // Ctrl+\

  // 忽略 SIGPIPE（写入已关闭的 socket 时触发）
  signal(SIGPIPE, SIG_IGN);

  LOG_INFO("Signal handlers registered (SIGINT, SIGTERM, SIGQUIT)");
  // std::cout << "[MprpcApplication] Signal handlers registered (SIGINT, SIGTERM, SIGQUIT)" << std::endl;
}

/**
 * @brief 信号处理函数（静态）
 * @param signal 信号编号
 * @details
 * 异步信号安全的处理逻辑：
 * 1. 打印信号信息
 * 2. 触发 Shutdown()
 * 3. 第二次收到信号时强制退出
 */
void MprpcApplication::SignalHandler(int signal) {
  static int signal_count = 0;
  signal_count++;

  const char* signal_name = "UNKNOWN";
  switch (signal) {
    case SIGINT:  signal_name = "SIGINT (Ctrl+C)"; break;
    case SIGTERM: signal_name = "SIGTERM"; break;
    case SIGQUIT: signal_name = "SIGQUIT"; break;
  }

  // 使用 write() 而不是 std::cout（信号处理器中更安全）
  char msg[256];
  snprintf(msg, sizeof(msg), "\n[Signal] Received %s (count: %d)\n", signal_name, signal_count);
  write(STDOUT_FILENO, msg, strlen(msg));

  if (signal_count == 1) {
    // 第一次收到信号，触发优雅关闭
    write(STDOUT_FILENO, "[Signal] Initiating graceful shutdown...\n", 42);
    GetInstance().Shutdown();
  } else {
    // 第二次收到信号，强制退出
    write(STDOUT_FILENO, "[Signal] Force exit!\n", 22);
    _exit(EXIT_FAILURE);
  }
}