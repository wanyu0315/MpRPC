# 测试
## MprpcApplication的功能测试

### 测试结果
#### 测试1——测试简单服务端启动配置
测试指令和结果
  developer@VM-4-16-ubuntu:~/MyRPC/bin$ ./test_app --config=../test/test.conf
  --- 1. Begin Init ---
  [MprpcApplication] Initializing MprpcFramework v1.0.0...
  [MprpcApplication] Loading config file: ../test/test.conf
  [MprpcApplication] Loading environment variables (RPC_* prefix)...
  [MprpcApplication] Initializing logging system...
    Log File:  stdout
    Log Level: INFO
  [MprpcApplication] Registering signal handlers...
  [MprpcApplication] Signal handlers registered (SIGINT, SIGTERM, SIGQUIT)
  [MprpcApplication] Framework initialized successfully!
  =================================================

  --- 2. Verify Configuration ---
  rpcserver.ip:   127.0.0.1
  rpcserver.port: 8000
  zookeeper.ip:   127.0.0.1

  --- 3. Verify Singleton ---
  Singleton works! Addr: 0x558f92eb7400

  [MprpcApplication] Shutting down gracefully...
  [MprpcApplication] Shutdown complete.

#### 测试2——测试空配置文件启动
框架支持 “零配置启动”。即允许用户不提供配置文件，通过默认路径查找，或者完全依赖环境变量，不会启动错误，只是会警告。
测试指令和结果
  developer@VM-4-16-ubuntu:~/MyRPC/bin$ ./test_app
  --- 1. Begin Init ---
  [MprpcApplication] Initializing MprpcFramework v1.0.0...
  [MprpcApplication] Warning: No config file found, using defaults
  [MprpcApplication] Loading environment variables (RPC_* prefix)...
  [MprpcApplication] Initializing logging system...
    Log File:  stdout
    Log Level: INFO
  [MprpcApplication] Registering signal handlers...
  [MprpcApplication] Signal handlers registered (SIGINT, SIGTERM, SIGQUIT)
  [MprpcApplication] Framework initialized successfully!
  =================================================

  --- 2. Verify Configuration ---
  rpcserver.ip:   
  rpcserver.port: 
  zookeeper.ip:   

  --- 3. Verify Singleton ---
  Singleton works! Addr: 0x5642fe8ef400

  [MprpcApplication] Shutting down gracefully...
  [MprpcApplication] Shutdown complete.

#### 测试3——测试环境变量配置
测试环境变量的优先性，当有环境变量配置时，以环境变量决定配置。
  测试命令
  developer@VM-4-16-ubuntu:~/MyRPC/bin$ export RPC_RPCSERVER_PORT=9999
  developer@VM-4-16-ubuntu:~/MyRPC/bin$ ./test_app --config=../test/test.conf
测试结果
  --- 1. Begin Init ---
  [MprpcApplication] Initializing MprpcFramework v1.0.0...
  [MprpcApplication] Loading config file: ../test/test.conf
  [MprpcApplication] Loading environment variables (RPC_* prefix)...
  [MprpcApplication] Initializing logging system...
    Log File:  stdout
    Log Level: INFO
  [MprpcApplication] Registering signal handlers...
  [MprpcApplication] Signal handlers registered (SIGINT, SIGTERM, SIGQUIT)
  [MprpcApplication] Framework initialized successfully!
  =================================================

  --- 2. Verify Configuration ---
  rpcserver.ip:   127.0.0.1
  rpcserver.port: 9999
  zookeeper.ip:   127.0.0.1

  --- 3. Verify Singleton ---
  Singleton works! Addr: 0x55cf7387f400

  [MprpcApplication] Shutting down gracefully...
  [MprpcApplication] Shutdown complete.