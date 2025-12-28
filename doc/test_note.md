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


## Zk功能测试
### 预备条件
  首先需要在本地安装并启动 ZooKeeper 的服务器：
    sudo apt-get update
    sudo apt-get install openjdk-11-jdk
    sudo apt-get install zookeeperd  # 注意是 zookeeperd，它会自动配置系统服务
    sudo netstat -tanp | grep 2181   # 检查端口 ZK 默认占用 2181 端口
  其次，安装的 zookeeperd 主要是服务端和 Java 库，不包含 C 语言开发所需的头文件。需要补装 C 语言开发包：
    sudo apt-get install libzookeeper-mt-dev

### 测试1：连接测试
测试目的
  主要是测试 Start() 函数，能否正常执行以下启动操作：启动zk客户端并同步连接上服务端；在zk服务端上创建rpc持节根节点。
对应测试结果：
  >>> [TEST] Start testing ZookeeperUtil...
  [ZkClient] Initialized with config:
    Host: 127.0.0.1:2181
    Session Timeout: 30000ms
    Root Path: /mprpc_test
  >>> [TEST] Connecting to Zookeeper Server...
  [ZkClient] Connecting to ZooKeeper: 127.0.0.1:2181
  2025-12-28 19:39:44,593:587042(0x7f5a415ed740):ZOO_INFO@log_env@753: Client environment:zookeeper.version=zookeeper C client 3.4.13
  2025-12-28 19:39:44,593:587042(0x7f5a415ed740):ZOO_INFO@log_env@757: Client environment:host.name=VM-4-16-ubuntu
  2025-12-28 19:39:44,593:587042(0x7f5a415ed740):ZOO_INFO@log_env@764: Client environment:os.name=Linux
  2025-12-28 19:39:44,593:587042(0x7f5a415ed740):ZOO_INFO@log_env@765: Client environment:os.arch=5.15.0-153-generic
  2025-12-28 19:39:44,593:587042(0x7f5a415ed740):ZOO_INFO@log_env@766: Client environment:os.version=#163-Ubuntu SMP Thu Aug 7 16:37:18 UTC 2025
  2025-12-28 19:39:44,594:587042(0x7f5a415ed740):ZOO_INFO@log_env@774: Client environment:user.name=developer
  2025-12-28 19:39:44,594:587042(0x7f5a415ed740):ZOO_INFO@log_env@782: Client environment:user.home=/home/developer
  2025-12-28 19:39:44,594:587042(0x7f5a415ed740):ZOO_INFO@log_env@794: Client environment:user.dir=/home/developer/MyRPC/build
  2025-12-28 19:39:44,594:587042(0x7f5a415ed740):ZOO_INFO@zookeeper_init@818: Initiating client connection, host=127.0.0.1:2181 sessionTimeout=30000 watcher=0x561cb6f4ec38 sessionId=0 sessionPasswd=<null> context=0x561cb6f5c2e0 flags=0
  2025-12-28 19:39:44,594:587042(0x7f5a415ec640):ZOO_INFO@check_events@1763: initiated connection to server [127.0.0.1:2181]
  2025-12-28 19:39:44,760:587042(0x7f5a415ec640):ZOO_INFO@check_events@1809: session establishment complete on server [127.0.0.1:2181], sessionId=0x10115be8af00000, negotiated timeout=30000
  [ZkClient] Watcher event: type=-1, state=3, path=
  [ZkClient] Connected to ZooKeeper!
  [ZkClient] Connected successfully!
  >>> [TEST] Connection established.
  [ZkClient] Node created: /mprpc_test
测试结果解析：
  连接与会话管理 (成功)
  [ZkClient] Connecting to ZooKeeper: 127.0.0.1:2181
  ... (中间是底层库的详细日志) ...
  [ZkClient] Connected to ZooKeeper!
  [ZkClient] Connected successfully!
  底层的 GlobalWatcher 成功捕获到了 ZOO_CONNECTED_STATE 事件，并且通过条件变量唤醒了主线程。“异步转同步”逻辑验证通过

### 测试2：基本节点操作测试
测试目的
  测试Create()、Get()、Set()基础节点操作。
对应测试结果：
  [ZkClient] Node created: /mprpc_test/node_1
  >>> [TEST] Create node success: /mprpc_test/node_1

  [ZkClient] Got data from node: /mprpc_test/node_1 (8 bytes)
  >>> [TEST] Get node value: hello_zk

  [ZkClient] Set data for node: /mprpc_test/node_1
  [ZkClient] Got data from node: /mprpc_test/node_1 (9 bytes)
  >>> [TEST] Set node new value: new_value
测试结果解析：
  Create()、Get()、Set() 功能逻辑工作正常，数据读写无误。

### 测试3：RPC 服务注册 (核心业务)测试
测试目的
  测试 RegisterService()函数能否把 server_name/server_addr 正确挂载到zk服务器的rpc根节点上；
  测试 GetServiceList(service_name)能否正常获取到service_name路径下的所有子节点（内部是使用GetChildren获取的，也顺带测试了GetChildren）
  测试PrintTree。
对应测试结果：
  RegisterService部分输出内容：
  >>> [TEST] Testing RPC Service Registration...
  [ZkClient] Registering service: /mprpc_test/UserService/127.0.0.1:8000
  [ZkClient] Node created: /mprpc_test/UserService
  [ZkClient] Node created: /mprpc_test/UserService/127.0.0.1:8000
  [ZkClient] Service registered successfully: /mprpc_test/UserService/127.0.0.1:8000
  >>> [TEST] Service registered: UserService @ 127.0.0.1:8000

  GetServiceList(service_name)部分输出内容：
  [ZkClient] Got 1 children for node: /mprpc_test/UserService
  [ZkClient] Found 1 instances for service: UserService
  >>> [TEST] Discovered instances for UserService: 1
      - 127.0.0.1:8000
  >>> [TEST] Found correct instance!

  PrintTree("/mprpc_test", 5)部分输出内容：
  ========== ZooKeeper Tree ===========
  ├─ /mprpc_test
  [ZkClient] Got 2 children for node: /mprpc_test
  │  ├─ /mprpc_test/node_1
  [ZkClient] Got 0 children for node: /mprpc_test/node_1
    ├─ /mprpc_test/UserService
  [ZkClient] Got 1 children for node: /mprpc_test/UserService
        ├─ /mprpc_test/UserService/127.0.0.1:8000
  [ZkClient] Got 0 children for node: /mprpc_test/UserService/127.0.0.1:8000
  =====================================

### 测试4：测试 Watch 机制
测试目的
  测试 WatchService(const std::string& service_name, ZkWatchCallback callback) 能否正常绑定 path 与对应的 Watch 回调函数 callback，并使用GetChildren 监听所有子节点的变化。
  测试 UnregisterService 能否正常注销（删除）临时服务节点。
  测试注销已经被 WatchService 监听的节点后，zk 客户端能否正常执行已经绑定注册的回调函数。

对应测试结果：
  WatchService部分对应内容：
  >>> [TEST] Testing Watch mechanism...
  [ZkClient] Watch registered for path: /mprpc_test/UserService
  [ZkClient] Got 1 children for node: /mprpc_test/UserService

  UnregisterService(service_name, ip_port)部分对应内容：
  >>> [TEST] Simulating service offline (Unregister)...
  [ZkClient] Unregistering service: /mprpc_test/UserService/127.0.0.1:8000
  成功 Watch 并回调：
  [ZkClient] Watcher event: type=4, state=3, path=/mprpc_test/UserService（ZkClient::OnWatcherEvent 成员函数打印的）
  >>> [CALLBACK] Path: /mprpc_test/UserService changed! Type: 4

  .Delete("/mprpc_test", true)的内容：
  [ZkClient] Got 2 children for node: /mprpc_test
  [ZkClient] Got 0 children for node: /mprpc_test/node_1
  [ZkClient] Deleted node: /mprpc_test/node_1
  [ZkClient] Got 0 children for node: /mprpc_test/UserService
  [ZkClient] Deleted node: /mprpc_test/UserService
  [ZkClient] Deleted node: /mprpc_test

  测试结束，ZkClient作用域结束自动析构（Stop()函数）
  >>> [TEST] All tests passed! Press Ctrl+C to exit.
  [ZkClient] Closing connection...
  2025-12-28 19:39:45,822:587042(0x7f5a415ed740):ZOO_INFO@zookeeper_close@2563: Closing zookeeper sessionId=0x10115be8af00000 to [127.0.0.1:2181]

  [ZkClient] Connection closed.

结果解读：
  调用了 UnregisterService（删除子节点）。
  ZK 服务端立刻感知，并发送通知。
  GlobalWatcher 收到通知 (type=4 即 ZOO_CHILD_EVENT)。
  OnWatcherEvent 成功在 map 中找到了回调函数。
  设置的 Lambda 回调被执行，打印了 [CALLBACK]。


  