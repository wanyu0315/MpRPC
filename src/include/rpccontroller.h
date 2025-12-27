#pragma once

#include <google/protobuf/service.h>
#include <google/protobuf/stubs/callback.h>
#include <string>

/**
 * @brief RPC 控制器
 * 用于客户端在 RPC 调用过程中：
 * 1. 传递控制参数（如超时时间、重试次数）
 * 2. 获取调用结果状态（成功/失败、错误信息）
 * 3. 控制取消操作
 */
class MprpcController : public google::protobuf::RpcController {
 public:
  MprpcController();
  ~MprpcController() override = default;

  // ============================================================================
  // google::protobuf::RpcController 核心接口重写
  // ============================================================================
  
  // 重置控制器状态，以便复用
  void Reset() override;

  // 判断 RPC 调用是否失败
  bool Failed() const override;

  // 获取错误信息
  std::string ErrorText() const override;

  // 设置失败状态和错误信息 (通常由框架调用)
  void SetFailed(const std::string& reason) override;

  // ============================================================================
  // 取消相关接口
  // ============================================================================
  
  // 尝试取消 RPC 调用
  void StartCancel() override;

  // 判断是否已被取消
  bool IsCanceled() const override;

  // 注册取消回调 (当调用被取消时触发)
  void NotifyOnCancel(google::protobuf::Closure* callback) override;

  // ============================================================================
  // 自定义扩展接口 (Getter/Setter)
  // 根据构造函数中初始化的成员变量，通常需要暴露设置接口供用户使用
  // ============================================================================
  
  void SetTimeout(int timeout_ms) { m_timeout_ms = timeout_ms; }
  int GetTimeout() const { return m_timeout_ms; }

  void SetRetryTimes(int retry_times) { m_retry_times = retry_times; }
  int GetRetryTimes() const { return m_retry_times; }

 private:
  // RPC方法执行过程中的状态
  bool m_failed;           // 是否失败
  std::string m_err_text;  // 错误描述
  bool m_canceled;         // 是否被取消

  // 扩展配置 (构造函数中出现的成员变量)
  int m_timeout_ms;   // 超时时间 (-1 表示无超时)
  int m_retry_times;  // 重试次数
};