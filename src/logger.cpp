#include "logger.h"
#include <time.h>
#include <iostream>

Logger& Logger::GetInstance()
{
    static Logger logger;
    return logger;
}

// 私有构造函数，启动日志写入线程
Logger::Logger()
{
    // 默认级别 INFO
    m_min_level.store(INFO); // 设置日志过滤级别，低于该级别的日志都会被屏蔽

    // 日志系统的“消费者”和“实际写入者”，后续的lambda表达式就是线程执行的函数体
    std::thread writeLogTask([this](){ // 使用 [this] 显式捕获，确保能访问 m_lckQue 等成员变量
        
        int last_day = -1;  // 记录上一次写入日志时的日期（天）
        FILE *pf = nullptr; // 日志文件指针，用于打开并写入日志文件

        for (;;)    // 开启线程死循环
        {
            // 变量名一致性：m_lckQue
            LogData data = m_lckQue.Pop();  // 有数据时返回一条 LogData 结构体（包含日志内容、级别、产生时间）。

            // 如果后续要支持优雅退出，这里需要判断一个 exit_flag
            // if (data.level == -1) break; 

            // 使用日志产生的时间
            tm *nowtm = localtime(&data.timestamp);

            // 检查日期变化，不一致说明进入新的一天，需要创建新日志文件。
            if (nowtm->tm_mday != last_day) 
            {
                // 关闭旧文件，避免资源泄漏
                if (pf != nullptr) {
                    fclose(pf);
                }

                // 日志文件名，格式示例：2026-1-2-log.txt
                char file_name[128];
                sprintf(file_name, "%d-%d-%d-log.txt", 
                        nowtm->tm_year + 1900, nowtm->tm_mon + 1, nowtm->tm_mday);

                pf = fopen(file_name, "a+"); // 以追加模式打开当天的日志文件
                // 如果文件打开失败，直接输出错误并退出程序
                if (pf == nullptr)
                {
                    // 这里不能再调 LOG_ERROR 了，否则死循环，直接 stderr
                    std::cerr << "logger file : " << file_name << " open error!" << std::endl;
                    exit(EXIT_FAILURE);
                }
                last_day = nowtm->tm_mday;
            }

            const char* level_str;
            // 把 LogLevel 枚举转换成对应字符串，后续用于写入日志头
            switch (data.level) {
                case INFO:  level_str = "INFO"; break;
                case ERROR: level_str = "ERROR"; break;
                case FATAL: level_str = "FATAL"; break;
                case DEBUG: level_str = "DEBUG"; break;
                default:    level_str = "UNKNOWN"; break;
            }

            // 生成日志头前缀，格式示例：19:05:32 => [INFO]
            char time_buf[128] = {0};
            sprintf(time_buf, "%02d:%02d:%02d => [%s] ", 
                    nowtm->tm_hour, nowtm->tm_min, nowtm->tm_sec, level_str);

            std::string final_msg = std::string(time_buf) + data.msg + "\n";    // 拼接最终写入的日志内容 = 日志头 + 日志正文 + 换行
            
            // 写入磁盘文件
            if (pf) {
                fputs(final_msg.c_str(), pf);   // 写字符串
                fflush(pf);     // 立刻刷入磁盘，提高可靠性
            }
        }
    });

    // 把后台线程设为分离线程（daemon线程），使其在后台持续运行，不阻塞主程序退出
    writeLogTask.detach();  
}

// 更新日志过滤等级
void Logger::SetFilterLevel(LogLevel level)
{
    m_min_level.store(level);
}

// 记录一条日志
void Logger::Log(LogLevel level, std::string msg)
{
    // 读取过滤等级，若当前日志级别低于最小允许级别，则直接丢弃，不入队
    if (level < m_min_level.load()) {
        return; 
    }

    // 构造一条日志消息对象 LogData
    LogData data;
    data.level = level;
    data.timestamp = time(nullptr); // 获取当前时间
    data.msg = std::move(msg);      // 移动语义，高效

    // 入队
    m_lckQue.Push(data);
}