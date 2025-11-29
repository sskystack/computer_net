#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <cstring>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <thread>
#include <mutex>
#include <sstream>
#include <string>

// Windows Socket 相关头文件
#ifdef _WIN32
    #include <winsock2.h>
    #pragma comment(lib, "ws2_32.lib")
#else
    #include <arpa/inet.h>
#endif

// ============================================
// 常量定义
// ============================================

// 数据包相关
#define HEADER_SIZE 32          // 协议头大小（字节）
#define DATA_SIZE 1400          // 单个数据包最大数据部分（字节）
#define MAX_PACKET_SIZE (HEADER_SIZE + DATA_SIZE)  // 最大包大小

// 窗口和缓冲相关
#define DEFAULT_WINDOW_SIZE 32  // 默认窗口大小（数据包个数）
#define MAX_WINDOW_SIZE 256     // 最大窗口大小

// 超时相关（毫秒）
#define RTO_INIT 1000           // 初始RTO
#define RTO_MIN 100             // 最小RTO
#define RTO_MAX 64000           // 最大RTO
#define TIMEOUT_CHECK 10        // 超时检查间隔

// RENO拥塞控制相关
#define MSS 1400                // 最大报文段大小
#define INITIAL_CWND 1          // 初始拥塞窗口（以MSS计）
#define SSTHRESH_INIT 16        // 初始慢启动阈值

// 序列号相关
#define SEQ_SPACE (1U << 32)    // 序列号空间大小 (2^32)

// ============================================
// 日志宏定义
// ============================================

enum LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO = 1,
    LOG_WARN = 2,
    LOG_ERROR = 3,
    LOG_NONE = 4
};

extern LogLevel g_log_level;
extern std::mutex g_log_mutex;

// 获取当前时间戳字符串
std::string GetTimeStamp();

// 日志输出函数
void Log(LogLevel level, const std::string& module, const std::string& message);

// 日志宏
#define LOG_DEBUG(module, msg) Log(LOG_DEBUG, module, msg)
#define LOG_INFO(module, msg) Log(LOG_INFO, module, msg)
#define LOG_WARN(module, msg) Log(LOG_WARN, module, msg)
#define LOG_ERROR(module, msg) Log(LOG_ERROR, module, msg)

// ============================================
// 时间测量工具
// ============================================

class Timer {
public:
    Timer() : start_time_(std::chrono::high_resolution_clock::now()) {}

    // 获取已经过的时间（毫秒）
    double ElapsedMs() const {
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration<double, std::milli>(now - start_time_).count();
    }

    // 获取已经过的时间（秒）
    double ElapsedSec() const {
        return ElapsedMs() / 1000.0;
    }

    // 重置计时器
    void Reset() {
        start_time_ = std::chrono::high_resolution_clock::now();
    }

private:
    std::chrono::high_resolution_clock::time_point start_time_;
};

// ============================================
// 工具函数
// ============================================

// 字节序转换
inline uint32_t HostToNetwork(uint32_t val) {
    return htonl(val);
}

inline uint32_t NetworkToHost(uint32_t val) {
    return ntohl(val);
}

inline uint16_t HostToNetwork(uint16_t val) {
    return htons(val);
}

inline uint16_t NetworkToHost(uint16_t val) {
    return ntohs(val);
}

// 序列号比较（考虑环绕）
inline bool IsSeqBefore(uint32_t seq1, uint32_t seq2) {
    return (int32_t)(seq1 - seq2) < 0;
}

inline bool IsSeqAfter(uint32_t seq1, uint32_t seq2) {
    return (int32_t)(seq1 - seq2) > 0;
}

inline bool IsSeqBeforeOrEqual(uint32_t seq1, uint32_t seq2) {
    return (int32_t)(seq1 - seq2) <= 0;
}

inline bool IsSeqAfterOrEqual(uint32_t seq1, uint32_t seq2) {
    return (int32_t)(seq1 - seq2) >= 0;
}

// 格式化输出
void PrintPacketInfo(const std::string& label, uint32_t seq, uint32_t ack,
                     uint8_t flags, uint16_t wnd, uint16_t len);

#endif // COMMON_H
