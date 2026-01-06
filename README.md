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
wget https://github.com/wanyu0315/MpRPC/archive/refs/tags/v1.0.0-alpha.zip -O MpRPC.zip
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