## 📝README

### 1. 依赖环境安装
在使用本框架前，请确保系统已安装以下依赖库。

#### 依赖库安装
使用者必须显式链接所有 Mprpc 依赖的底层库。

**1. Protobuf & Pthread & Zookeeper 客户端**
```Bash
# 安装 Protobuf 编译器和开发库
sudo apt-get install protobuf-compiler libprotobuf-dev

# 安装 Zookeeper 开发库 (必须安装，否则无法链接)
# 作用：提供 C 语言 API 供框架进行服务注册与发现
sudo apt-get install libzookeeper-mt-dev 
```

**2. Muduo 网络库** *由于 Ubuntu 官方源可能不包含 Muduo，建议从源码安装：*
```Bash
git clone [https://github.com/chenshuo/muduo.git](https://github.com/chenshuo/muduo.git)
cd muduo
./build.sh
./build.sh install
```

**3. Spdlog & Fmt 日志库**
```Bash
sudo apt-get install libspdlog-dev libfmt-dev
```
------

### 2. 下载与安装框架
确保当前处于你有权限的目录（如用户主目录）：
```Bash
cd ~
```
#### 第一步：下载源码
*(请替换为你的真实下载链接)*
```Bash
wget https://github.com/wanyu0315/MpRPC/archive/refs/tags/v1.0.2.zip -O MpRPC.zip
```

#### 第二步：解压
```Bash
unzip MpRPC.zip
cd MpRPC
```

#### 第三步：一键安装
执行安装脚本，该脚本会自动清理、编译并将库安装到系统路径。
```Bash
sudo chmod +x MpRPC_install.sh
./MpRPC_install.sh
```
*注：脚本内部包含 `sudo` 操作，执行过程中可能需要输入密码。*
默认安装路径：
- 头文件：`/usr/local/include/mprpc`
- 库文件：`/usr/local/lib/libmprpc.a`
------

### 3. 如何在你的项目中使用
本框架源码使用的是静态库 (.a)，在你的项目 `CMakeLists.txt` 中，你需要链接 `mprpc` 以及它所有的底层依赖。

**CMakeLists.txt 示例模板：**
```CMake
cmake_minimum_required(VERSION 3.xx)
project(your_project)

# 1. 基础设置
set(CMAKE_CXX_STANDARD 17)

# 2. 查找必要的依赖
find_package(Protobuf REQUIRED)
find_package(Threads REQUIRED) # 对应 pthread
# fmt 和 spdlog 通常支持 find_package
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)

# 注意：Muduo 和 Zookeeper 通常需要直接查找库文件或直接链接
# 如果你的 Muduo 安装在标准路径，通常不需要特殊配置

# 3. 添加你的可执行文件
add_executable(your_exe main.cpp)

# 4. 链接库 (顺序很重要)
target_link_libraries(your_exe 
    mprpc           # 我们的 RPC 框架
    muduo_net       # Muduo 网络库
    muduo_base      # Muduo 基础库
    zookeeper_mt    # Zookeeper 多线程客户端库
    fmt::fmt        # 格式化库
    spdlog::spdlog  # 日志库
    Threads::Threads # 线程库
    ${Protobuf_LIBRARIES} # Protobuf
)
```

#### 关于 ZooKeeper 的特别说明

1. **开发库**：必须安装 `libzookeeper-mt-dev`，在 CMake 中直接链接 `zookeeper_mt` 即可，**不需要**写 `find_package(zookeeper_mt)`，因为`libzookeeper-mt-dev` 是一个比较古老的 C 语言库，不支持直接写 `find_package(xxx)`。如果要显式包含可以使用`find_library(ZOOKEEPER_LIB zookeeper_mt)`。

2. **服务端**：如果你需要在本地进行调试，需要安装 ZK 服务端：
   ```Bash
   sudo apt-get install zookeeperd
   ```
   *注意：`zookeeperd` 只是服务端程序，编译 C++ 代码依然需要 `libzookeeper-mt-dev`。*

3. **头文件引用**：如果在代码中直接使用 ZK，请包含 `<zookeeper/zookeeper.h>`。

------

### 4. 高级配置：修改安装路径
默认情况下，`MpRPC_install.sh` 会将框架安装到 `/usr/local`。如果你希望安装到自定义路径（例如 `/usr/local/mprpc`），请不要直接运行脚本，而是**手动执行**以下构建命令：

```Bash
# 1. 清理并创建构建目录
rm -rf build && mkdir build && cd build

# 2. 指定安装路径 (CMAKE_INSTALL_PREFIX)
cmake -DCMAKE_INSTALL_PREFIX=/usr/local/mprpc ..

# 3. 编译与安装
make -j4
sudo make install
```
**注意：** 如果你安装到了非标准路径（如 `/usr/local/mprpc`），在你的业务项目 CMake 中需要显式指定搜索路径：

```CMake
# 消费者的 CMakeLists.txt
include_directories(/usr/local/mprpc/include)
link_directories(/usr/local/mprpc/lib)

add_executable(consumer main.cpp)
target_link_libraries(consumer mprpc ...)
```

## 📖 使用指南 (Usage)
本框架的使用流程分为四个步骤：定义 Protobuf 服务、编写服务端 (Provider)、编写客户端 (Consumer) 以及配置文件。

### 1. 定义 Protobuf 服务接口 (`user.proto`)

首先，使用 Protocol Buffers 定义 RPC 服务接口和消息类型。

```protobuf
syntax = "proto3";

package fixbug;

// 定义结果码消息
message ResultCode {
    int32 errcode = 1;
    string errmsg = 2;
}

// 定义登录请求和响应
message LoginRequest {
    string name = 1;
    string pwd = 2;
}

message LoginResponse {
    ResultCode result = 1;
    bool success = 2;
}

// 定义注册请求和响应
message RegisterRequest {
    uint32 id = 1;
    string name = 2;
    string pwd = 3;
}

message RegisterResponse {
    ResultCode result = 1;
    bool success = 2;
}

// 定义 RPC 服务接口
service UserServiceRpc {
    rpc Login(LoginRequest) returns(LoginResponse);
    rpc Register(RegisterRequest) returns(RegisterResponse);
}
```

------

### 2. 服务端开发 
服务端需要继承 Protobuf 生成的抽象类，并实现具体的业务逻辑。

**核心步骤：**
1. 初始化框架 (`MprpcApplication::Init`)。
2. 配置 `RpcProvider` (监听端口、连接数等)。
3. 注册服务 (`NotifyService`)。
4. 启动服务 (`Run`)。
5. 使用 `_exit(0)` 确保在涉及 Zookeeper 库时安全退出。

```cpp
#include <iostream>
#include <string>
#include "user.pb.h"
#include "mprpcapplication.h"
#include "rpcprovider.h"

// 1. 定义业务类，继承自生成的 RPC 服务虚基类
class UserService : public fixbug::UserServiceRpc {
public:
    // --- 本地业务逻辑 ---
    bool Login(std::string name, std::string pwd) {
        std::cout << "Doing local service: Login" << std::endl;
        std::cout << "name:" << name << " pwd:" << pwd << std::endl;
        return true;
    }

    bool Register(uint32_t id, std::string name, std::string pwd) {
        std::cout << "Doing local service: Register" << std::endl;
        std::cout << "id:" << id << " name:" << name << " pwd:" << pwd << std::endl;
        return true;
    }

    // --- 重写 Protobuf 虚函数，供框架调用 ---
    void Login(::google::protobuf::RpcController* controller,
               const ::fixbug::LoginRequest* request,
               ::fixbug::LoginResponse* response,
               ::google::protobuf::Closure* done) override {
        // 1. 获取参数
        std::string name = request->name();
        std::string pwd = request->pwd();

        // 2. 执行本地业务
        bool login_result = Login(name, pwd);

        // 3. 写入响应
        response->mutable_result()->set_errcode(0);
        response->mutable_result()->set_errmsg("");
        response->set_success(login_result);

        // 4. 执行回调（发送响应）
        done->Run();
    }

    void Register(::google::protobuf::RpcController* controller,
                  const ::fixbug::RegisterRequest* request,
                  ::fixbug::RegisterResponse* response,
                  ::google::protobuf::Closure* done) override {
        uint32_t id = request->id();
        std::string name = request->name();
        std::string pwd = request->pwd();

        bool ret = Register(id, name, pwd);

        response->mutable_result()->set_errcode(0);
        response->mutable_result()->set_errmsg("");
        response->set_success(ret);

        done->Run();
    }
};

int main(int argc, char **argv) {
    // 1. 框架初始化
    MprpcApplication::Init(argc, argv);

    // 2. 配置 Provider
    RpcProvider::Config config;
    config.ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserver_ip");
    config.port = atoi(MprpcApplication::GetInstance().GetConfig().Load("rpcserver_port").c_str());
    config.thread_num = 4;        // IO 线程数
    config.max_connections = 10000; 

    // 3. 启动 Provider
    RpcProvider provider(config);
    provider.NotifyService(new UserService()); // 发布服务
    
    // 4. 阻塞运行，等待远程请求
    provider.Run();

    // 5. 优雅退出 (可选，通常 Run() 是阻塞的，只有被信号中断才会到这里)
    MprpcApplication::GetInstance().Shutdown();
    _exit(0); // 推荐使用 _exit(0) 以避免 Zookeeper 库的静态析构问题
}
```

------

### 3. 客户端开发 (Consumer)
客户端通过 `MprpcChannel` 连接服务，支持 Zookeeper 服务发现和连接池复用。

**最佳实践：**
- **MprpcChannel 复用**：`MprpcChannel` 内部维护了连接池和 Zookeeper 客户端，**应当作为长生命周期对象复用**（如在 `main` 函数栈上定义，或单例管理），避免频繁创建销毁。
- **Stub 调用**：使用 Protobuf 生成的 Stub 类进行 RPC 调用。
- **优雅退出**：使用 `_exit(0)` 结束进程，跳过 Zookeeper C Client 老版本潜在的析构崩溃问题。

```cpp
#include <iostream>
#include "user.pb.h"
#include "mprpcapplication.h"
#include "rpcclient.h"

int main(int argc, char **argv) {
    // 1. 初始化框架
    MprpcApplication::Init(argc, argv);

    // 2. 配置客户端
    RpcClientConfig client_config;
    client_config.rpc_timeout_ms = 5000;
    client_config.connection_pool_size = 4; // 连接池大小

    // 3. 创建 Channel (长生命周期对象，复用)
    // 传入空 IP/Port 即开启 Zookeeper 服务发现模式
    MprpcChannel channel("", 0, client_config);

    // 4. 创建 Stub 对象 (存根)
    fixbug::UserServiceRpc_Stub stub(&channel);

    // 5. 构造请求
    fixbug::LoginRequest login_req;
    login_req.set_name("lzz");
    login_req.set_pwd("123456");
    fixbug::LoginResponse login_resp;
    
    // 6. 发起 RPC 同步调用
    MprpcController controller; 
    stub.Login(&controller, &login_req, &login_resp, nullptr);

    // 7. 处理响应
    if (controller.Failed()) {
        std::cout << "Rpc Failed: " << controller.ErrorText() << std::endl;
    } else {
        if (login_resp.result().errcode() == 0) {
            std::cout << "Rpc Success! Login result: " << login_resp.success() << std::endl;
        } else {
            std::cout << "Business Error: " << login_resp.result().errmsg() << std::endl;
        }
    }

    // 8. 退出程序
    // 显式 ShutDown 回收资源，使用 _exit(0) 安全退出
    MprpcApplication::GetInstance().Shutdown();
    _exit(0); 
}
```

------

### 4. 配置文件 (`test.conf`)

框架启动时需要加载配置文件，用于指定 IP、端口以及 Zookeeper 地址。
```ini
# rpc节点的ip地址
rpcserver_ip=127.0.0.1
# rpc节点的port端口
rpcserver_port=8000

# zookeeper服务ip
zookeeper_ip=127.0.0.1
# zookeeper服务port
zookeeper_port=2181
```

### 5. 编译与运行

**启动 ZooKeeper:** 确保 Zookeeper 服务已启动。
**运行服务端:**
```Bash
./provider --config=../test.conf --log=../log_file/provider_log.log --log_level=INFO 
```
**运行客户端:**
```Bash
./consumer --config=../test.conf --log=../log_file/consumer_log.log --log_level=INFO 
```
注意指令的格式，以`--`衔接，提供`config`、`log`、l`og_level`以及`--daemon`参数。
使用`--help`和查看命令帮助。


## 实际使用时候的一些疑问：

1. 静态资源构造函数中没有构造，为什么不显式构造。
   **全局变量**不是类成员，当编译这个 `.cpp` 文件时，编译器就会在**全局/静态存储区**为它们分配内存，不需要你自己分配。

2. `MprpcChannel`依赖于`ConnectionPool`、`RpcConnection`和`ZkClient`，为什么在使用实例中不需要构造它们呢？
   - 首先关于`ConnectionPool`和`RpcConnection`：
     如果是直连模式，显式指定了RPC_Server的 IP 和 Port，`MprpcChannel`的构造函数中实现了`ConnectionPool`和`RpcConnection`的创建与配置的。
     如果是 ZK 服务发现模式，在RPC 调用统一入口`CallMethod`函数中实现了`ConnectionPool`和`RpcConnection`的创建与配置。

   - 关于`ZkClient`：
     无论是什么模式，都是在`CallMethod`函数中实现`ZkClient`的创建、启动与连接 ZK 服务器。

3. `config`声明的配置参数并没有全部设置，会导致什么后果?
   在定义结构体时，使用了 **C++11 的“类内成员初始化” 特性。这意味着如果你不手动设置某个字段，它就会自动使用你写在 `.h` 文件里的**默认值。