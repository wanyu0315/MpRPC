#pragma once
#include "lockqueue.h"
#include <string>
#include <vector>
#include <atomic>

enum LogLevel
{
    INFO,  // 普通信息
    ERROR, // 错误信息
    FATAL, // 致命错误
    DEBUG  // 调试信息
};

// 内部日志数据结构
struct LogData {
    std::string msg;
    LogLevel level;
    time_t timestamp; // 记录日志产生的时间
};

class Logger
{
public:
    static Logger& GetInstance();
    void SetFilterLevel(LogLevel level);
    void Log(LogLevel level, std::string msg);

private:
    std::atomic<int> m_min_level; 
    LockQueue<LogData> m_lckQue;  // 名字确认为 m_lckQue

    Logger();
    Logger(const Logger&) = delete;
    Logger(Logger&&) = delete;
};

// 宏定义
// 使用 ##__VA_ARGS__ 处理可变参数，支持无参调用
#define LOG_BASE(level, format, ...) \
    do \
    { \
        Logger &logger = Logger::GetInstance(); \
        char c[1024] = {0}; \
        snprintf(c, 1024, format, ##__VA_ARGS__); \
        logger.Log(level, c); \
    } while(0)

#define LOG_INFO(format, ...)  LOG_BASE(INFO, format, ##__VA_ARGS__)
#define LOG_ERROR(format, ...) LOG_BASE(ERROR, format, ##__VA_ARGS__)

#define LOG_FATAL(format, ...) \
    do \
    { \
        LOG_BASE(FATAL, format, ##__VA_ARGS__); \
        exit(EXIT_FAILURE); \
    } while(0)

#ifdef DEBUG_MODE
#define LOG_DEBUG(format, ...) LOG_BASE(DEBUG, format, ##__VA_ARGS__)
#else
#define LOG_DEBUG(format, ...)
#endif