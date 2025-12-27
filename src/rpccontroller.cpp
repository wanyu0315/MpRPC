#include "rpccontroller.h"

// ============================================================================
// 构造函数
// ============================================================================
MprpcController::MprpcController() 
    : m_failed(false), 
      m_err_text(""),
      m_timeout_ms(-1),
      m_retry_times(0),
      m_canceled(false) {
  // 初始化为未失败状态
}

// ============================================================================
// 核心接口实现
// ============================================================================

void MprpcController::Reset() {
  // 重置所有状态，允许控制器被复用
  m_failed = false;
  m_err_text.clear();
  m_canceled = false;
  // 不重置 timeout 和 retry_times，这些是配置项
}

bool MprpcController::Failed() const {
  return m_failed;
}

std::string MprpcController::ErrorText() const {
  return m_err_text;
}

void MprpcController::SetFailed(const std::string& reason) {
  m_failed = true;
  m_err_text = reason;
}

// ============================================================================
// 取消相关接口 (预留实现)
// ============================================================================

void MprpcController::StartCancel() {
  // TODO: 实现取消逻辑
  // 1. 设置 m_canceled = true
  // 2. 通知网络层中断发送/接收
  // 3. 触发注册的取消回调
  m_canceled = true;
}

bool MprpcController::IsCanceled() const {
  return m_canceled;
}

void MprpcController::NotifyOnCancel(google::protobuf::Closure* callback) {
  // TODO: 实现取消通知
  // 1. 保存 callback 到内部列表
  // 2. 当 StartCancel() 被调用时，执行所有注册的 callback
  
  // 当前未实现，直接忽略
  (void)callback;  // 避免未使用参数警告
}