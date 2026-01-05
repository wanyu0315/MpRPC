/**
 * @file mprpcapplication.h
 * @brief RPC 框架的应用程序管理类
 * @details
 * 职责：
 * 1. 框架初始化（配置加载、日志初始化）
 * 2. 全局单例访问
 * 3. 命令行参数解析
 * 4. 优雅关闭机制
 * 5. 错误处理
 * 
 * 使用示例：
 * ```cpp
 * int main(int argc, char* argv[]) {
 *   // 1. 初始化框架
 *   MprpcApplication::Init(argc, argv);
 *   
 *   // 2. 获取配置
 *   auto& config = MprpcApplication::GetInstance().GetConfig();
 *   int port = config.LoadInt("RpcServer.port", 8080);
 *   
 *   // 3. 启动服务
 *   RpcProvider provider;
 *   provider.Run();
 *   
 *   return 0;
 * }
 * ```
 */

#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "mprpcconfig.h"

// 引入 spdlog 头文件
#define SPDLOG_ACTIVE_LEVEL SPDLOG_LEVEL_TRACE // 开启所有级别的编译
#include <spdlog/spdlog.h>

//  解除 Muduo 或其他库可能存在的宏定义冲突
#ifdef LOG_TRACE
  #undef LOG_TRACE
#endif
#ifdef LOG_DEBUG
  #undef LOG_DEBUG
#endif
#ifdef LOG_INFO
  #undef LOG_INFO
#endif
#ifdef LOG_WARN
  #undef LOG_WARN
#endif
#ifdef LOG_ERROR
  #undef LOG_ERROR
#endif
#ifdef LOG_CRITICAL
  #undef LOG_CRITICAL
#endif

//  定义方便的日志宏，直接映射到 spdlog 的宏
// 使用这些宏可以保留文件行号信息，并且是零成本抽象
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
#define LOG_CRITICAL(...) SPDLOG_CRITICAL(__VA_ARGS__)

// ============================================================================
// 框架启动参数
// ============================================================================
/**
 * @brief 命令行参数解析结果
 */
struct CommandLineArgs {
  std::string config_file;        // 配置文件路径 (--config=xxx.ini)
  std::string log_file;           // 日志文件路径 (--log=xxx.log)
  std::string log_level;          // 日志级别 (--log_level=INFO)
  bool daemon_mode = false;       // 是否后台运行 (--daemon)
  bool help = false;              // 是否显示帮助 (--help)
  
  // 自定义扩展参数 (例如: --key=value)
  std::unordered_map<std::string, std::string> custom_args;
};

// ============================================================================
// RPC 应用程序管理类
// ============================================================================
/**
 * @brief RPC 框架的全局应用程序管理器 (单例模式)
 * @details
 * 核心功能：
 * 1. 框架初始化：Init(argc, argv)
 * 2. 配置管理：GetConfig()
 * 3. 优雅关闭：RegisterShutdownHook()
 * 4. 信号处理：自动捕获 SIGINT/SIGTERM
 * 
 * 设计模式：
 * - 单例模式：全局唯一实例
 * - 观察者模式：关闭钩子回调
 * 
 * 线程安全：是（内部使用互斥锁）
 */
class MprpcApplication {
 public:
  // ========== 单例访问 ==========
  
  /**
   * @brief 获取全局单例实例
   * @return 应用程序单例引用
   */
  static MprpcApplication& GetInstance();

  // ========== 框架初始化 ==========
  
  /**
   * @brief 初始化 RPC 框架 (必须在 main 函数开头调用)
   * @param argc 命令行参数个数
   * @param argv 命令行参数数组
   * @details
   * 执行流程：
   * 1. 解析命令行参数
   * 2. 加载配置文件
   * 3. 加载环境变量 (RPC_ 前缀)
   * 4. 初始化日志系统
   * 5. 注册信号处理器
   * 
   * @example
   * ```cpp
   * int main(int argc, char* argv[]) {
   *   MprpcApplication::Init(argc, argv);
   *   // ...
   * }
   * ```
   */
  static void Init(int argc, char* argv[]);

  /**
   * @brief 检查框架是否已初始化
   * @return true 已初始化, false 未初始化
   */
  static bool IsInitialized();

  // ========== 配置访问 ==========
  
  /**
   * @brief 获取配置管理器
   * @return 配置管理器引用
   * @note 调用前必须先调用 Init()
   */
  MprpcConfig& GetConfig();

  /**
   * @brief 获取命令行参数
   * @return 命令行参数结构体
   */
  const CommandLineArgs& GetArgs() const;

  // ========== 生命周期管理 ==========
  
  /**
   * @brief 注册关闭钩子 (优雅关闭时调用)
   * @param hook 关闭回调函数
   * @return 钩子 ID (用于取消注册)
   * @details
   * 当收到 SIGINT/SIGTERM 信号或调用 Shutdown() 时，
   * 会按注册顺序**逆序**调用所有钩子
   * 
   * @example
   * ```cpp
   * MprpcApplication::GetInstance().RegisterShutdownHook([]() {
   *   std::cout << "Cleanup resources..." << std::endl;
   * });
   * ```
   */
  int RegisterShutdownHook(std::function<void()> hook);

  /**
   * @brief 取消注册关闭钩子
   * @param hook_id 钩子 ID
   */
  void UnregisterShutdownHook(int hook_id);

  /**
   * @brief 触发优雅关闭
   * @details
   * 1. 设置关闭标志
   * 2. 调用所有注册的关闭钩子
   * 3. 可以在信号处理器中调用
   */
  void Shutdown();

  /**
   * @brief 检查是否正在关闭
   * @return true 正在关闭, false 正常运行
   */
  bool IsShuttingDown() const;

  // ========== 辅助功能 ==========
  
  /**
   * @brief 打印框架版本信息
   */
  static void PrintVersion();

  /**
   * @brief 打印使用帮助
   */
  static void PrintHelp();

  /**
   * @brief 打印所有配置项 (调试用)
   */
  void PrintConfig() const;

 private:
  // ========== 私有构造/析构 (单例模式) ==========
  
  MprpcApplication();
  ~MprpcApplication();

  // 禁止拷贝和赋值
  MprpcApplication(const MprpcApplication&) = delete;
  MprpcApplication& operator=(const MprpcApplication&) = delete;

  // ========== 内部方法 ==========
  
  /**
   * @brief 解析命令行参数
   * @param argc 参数个数
   * @param argv 参数数组
   * @return 解析结果
   */
  CommandLineArgs ParseCommandLine(int argc, char* argv[]);

  /**
   * @brief 初始化日志系统
   * @param log_file 日志文件路径
   * @param log_level 日志级别 (DEBUG/INFO/WARN/ERROR)
   */
  void InitLogging(const std::string& log_file, const std::string& log_level);
  void InitLoggingAsync(const std::string& log_file, const std::string& log_level);

  /**
   * @brief 注册信号处理器 (捕获 SIGINT/SIGTERM)
   */
  void RegisterSignalHandlers();

  /**
   * @brief 信号处理函数 (静态)
   * @param signal 信号编号
   */
  static void SignalHandler(int signal);

  // ========== 成员变量 ==========
  
  static std::atomic<bool> s_initialized_;      // 是否已初始化
  static std::atomic<bool> s_shutting_down_;    // 是否正在关闭

  MprpcConfig& config_;                           // 配置管理器
  CommandLineArgs args_;                         // 命令行参数

  // 关闭钩子管理
  std::mutex shutdown_hooks_mutex_;
  int next_hook_id_ = 1;
  std::vector<std::pair<int, std::function<void()>>> shutdown_hooks_;

  // 框架信息
  static constexpr const char* FRAMEWORK_NAME = "MprpcFramework";
  static constexpr const char* FRAMEWORK_VERSION = "1.0.0";
};

// ============================================================================
// 便捷宏定义
// ============================================================================

/**
 * @brief 快速获取配置项 (字符串)
 * @example CONFIG_GET("RpcServer.ip", "0.0.0.0")
 */
#define CONFIG_GET(key, default_value) \
  MprpcApplication::GetInstance().GetConfig().Load(key)

/**
 * @brief 快速获取配置项 (整数)
 * @example CONFIG_GET_INT("RpcServer.port", 8080)
 */
#define CONFIG_GET_INT(key, default_value) \
  MprpcApplication::GetInstance().GetConfig().LoadInt(key, default_value)

/**
 * @brief 检查框架是否初始化
 * @example CHECK_INITIALIZED()
 */
#define CHECK_INITIALIZED() \
  do { \
    if (!MprpcApplication::IsInitialized()) { \
      std::cerr << "[MprpcApplication] Error: Framework not initialized! " \
                << "Call MprpcApplication::Init() first." << std::endl; \
      exit(EXIT_FAILURE); \
    } \
  } while(0)