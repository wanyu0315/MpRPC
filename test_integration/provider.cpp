#include <iostream>
#include <string>
#include "user.pb.h"
#include <mprpc/mprpcapplication.h>
#include <mprpc/rpcprovider.h>

using namespace fixbug;

// 1. 定义本地业务类
class UserService : public UserServiceRpc {
public:
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

    // 2. 重写 Protobuf 的虚函数，这是框架调用的入口
    void Login(::google::protobuf::RpcController* controller,
               const ::fixbug::LoginRequest* request,
               ::fixbug::LoginResponse* response,
               ::google::protobuf::Closure* done) override 
    {
        // 框架帮我们反序列化好了 request，我们直接用
        std::string name = request->name();
        std::string pwd = request->pwd();

        // 做本地业务
        bool login_result = Login(name, pwd);

        // 写入响应
        ResultCode* code = response->mutable_result();
        code->set_errcode(0);
        code->set_errmsg("");
        response->set_success(login_result);

        // 执行回调 (序列化 + 网络发送)
        done->Run();
    }

    void Register(::google::protobuf::RpcController* controller,
                  const ::fixbug::RegisterRequest* request,
                  ::fixbug::RegisterResponse* response,
                  ::google::protobuf::Closure* done) override 
    {
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

// ==========================================================
//    主函数
//    需要适配 RpcProvider 的 Config 构造方式
// ==========================================================
int main(int argc, char **argv) {
    // 1. 初始化框架 (读取配置文件/环境变量、初始化日志等等)
    // MprpcApplication 负责解析命令行参数 (-i test.conf) 并加载配置文件到内存
    MprpcApplication::Init(argc, argv);

    // 2. 准备 RpcProvider 的配置
    RpcProvider::Config config;
    
    // 从 MprpcApplication 的全局配置中提取 IP 和 Port
    // 假设配置文件中有 rpcserver_ip 和 rpcserver_port 这两个 key
    config.ip = MprpcApplication::GetInstance().GetConfig().Load("rpcserver_ip");
    
    std::string port_str = MprpcApplication::GetInstance().GetConfig().Load("rpcserver_port");
    config.port = static_cast<uint16_t>(std::atoi(port_str.c_str()));   // rpcprovider的port接收的是整数
    
    // 你也可以在这里手动设置其他高级参数，或者也从配置文件读取
    config.thread_num = 4; 
    config.max_connections = 10000;
    config.max_message_size = 10 * 1024 * 1024; // 最大消息大小限制
    config.idle_timeout_seconds = 300;  // 空闲连接超时时间，配合 CheckIdleConnections 清理死连接
    config.enable_metrics = true; // 是否开启指标统计

    // 3. 实例化 Provider
    // 使用配置对象构造
    RpcProvider provider(config);

    // 4. 发布服务
    // 将 UserService 对象注册到 provider 中
    provider.NotifyService(new UserService());

    // 5. 启动服务 (阻塞)
    provider.Run();

    // 6. 显式清理全局单例
    MprpcApplication::GetInstance().Shutdown();
    spdlog::shutdown();

    return 0;
}