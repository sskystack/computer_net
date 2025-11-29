#include "common.h"
#include <iostream>

// 全局日志级别
LogLevel g_log_level = LOG_INFO;
std::mutex g_log_mutex;

// 获取当前时间戳字符串
std::string GetTimeStamp() {
    auto now = std::chrono::system_clock::now();
    auto time_t_now = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;

    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t_now), "%H:%M:%S")
       << '.' << std::setfill('0') << std::setw(3) << ms.count();
    return ss.str();
}

// 日志输出函数
void Log(LogLevel level, const std::string& module, const std::string& message) {
    if (level < g_log_level) {
        return;
    }

    std::lock_guard<std::mutex> lock(g_log_mutex);

    std::string level_str;
    switch (level) {
        case LOG_DEBUG: level_str = "DEBUG"; break;
        case LOG_INFO:  level_str = "INFO "; break;
        case LOG_WARN:  level_str = "WARN "; break;
        case LOG_ERROR: level_str = "ERROR"; break;
        default:        level_str = "UNKN "; break;
    }

    std::cout << "[" << GetTimeStamp() << "] [" << level_str << "] ["
              << module << "] " << message << std::endl;
}

// 格式化输出数据包信息
void PrintPacketInfo(const std::string& label, uint32_t seq, uint32_t ack,
                     uint8_t flags, uint16_t wnd, uint16_t len) {
    std::string flag_str;
    if (flags & 0x01) flag_str += "SYN ";
    if (flags & 0x02) flag_str += "ACK ";
    if (flags & 0x04) flag_str += "FIN ";
    if (flags & 0x08) flag_str += "RST ";
    if (flags & 0x10) flag_str += "DATA";

    std::cout << label << " | seq=" << seq << " ack=" << ack
              << " flags=" << flag_str << " wnd=" << wnd
              << " len=" << len << std::endl;
}
