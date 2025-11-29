#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>

// ============================================
// 协议标志位定义
// ============================================

// 标志位（8位字段）
#define FLAG_SYN  0x01      // 同步标志（连接建立）
#define FLAG_ACK  0x02      // 确认标志
#define FLAG_FIN  0x04      // 结束标志（连接关闭）
#define FLAG_RST  0x08      // 重置标志（异常重置连接）
#define FLAG_DATA 0x10      // 数据标志

// ============================================
// 协议头结构体
// ============================================

/**
 * 协议头结构，共32字节（固定大小）
 *
 * 字段说明：
 * - seq: 序列号（发送方的序列号）
 * - ack: 确认号（期望接收的下一个序列号）
 * - flags: 标志位（SYN|ACK|FIN|RST|DATA）
 * - wnd: 接收窗口大小（通知对方本方的接收窗口）
 * - len: 数据长度（不包括头部）
 * - checksum: 校验和（用于差错检测）
 */
struct ProtocolHeader {
    uint32_t seq;           // 序列号（4字节）
    uint32_t ack;           // 确认号（4字节）
    uint8_t flags;          // 标志位（1字节）
    uint8_t reserved1;      // 保留字段（1字节）
    uint16_t wnd;           // 接收窗口大小（2字节）
    uint16_t len;           // 数据长度（2字节）
    uint32_t checksum;      // 校验和（4字节）
    uint8_t reserved2[8];   // 保留字段（8字节）
    // 总计：32字节

    // 初始化方法
    ProtocolHeader() : seq(0), ack(0), flags(0), reserved1(0),
                       wnd(0), len(0), checksum(0) {
        std::memset(reserved2, 0, sizeof(reserved2));
    }

    // 重置所有字段
    void Reset() {
        seq = 0;
        ack = 0;
        flags = 0;
        reserved1 = 0;
        wnd = 0;
        len = 0;
        checksum = 0;
        std::memset(reserved2, 0, sizeof(reserved2));
    }

} __attribute__((packed));  // 保证紧密打包，大小为32字节

// 静态断言验证头部大小
static_assert(sizeof(ProtocolHeader) == 32, "ProtocolHeader size must be 32 bytes");

// ============================================
// 协议常量
// ============================================

// 连接状态定义
enum ConnectionState {
    STATE_CLOSED = 0,           // 关闭状态
    STATE_LISTEN = 1,           // 监听状态
    STATE_SYN_SENT = 2,         // 已发送SYN，等待SYN-ACK
    STATE_SYN_RECV = 3,         // 已接收SYN，已发送SYN-ACK
    STATE_ESTABLISHED = 4,      // 连接建立
    STATE_FIN_WAIT_1 = 5,       // 已发送FIN，等待ACK
    STATE_FIN_WAIT_2 = 6,       // 已接收ACK，等待对端FIN
    STATE_CLOSING = 7,          // 同时关闭
    STATE_TIME_WAIT = 8,        // 时间等待状态
    STATE_CLOSE_WAIT = 9,       // 被动关闭，已接收FIN，等待关闭
    STATE_LAST_ACK = 10         // 等待最后的ACK
};

// RENO拥塞控制状态
enum CongestionState {
    CONG_SLOW_START = 0,        // 慢启动
    CONG_CONGESTION_AVOIDANCE = 1, // 拥塞避免
    CONG_FAST_RECOVERY = 2      // 快速恢复
};

// ============================================
// 数据包类型定义
// ============================================

/**
 * 完整的数据包结构 = 头部 + 数据
 * 最大总大小为 1432 字节（32字节头部 + 1400字节数据）
 */
struct Packet {
    ProtocolHeader header;          // 协议头（32字节）
    uint8_t data[1400];             // 数据部分（最多1400字节）

    Packet() {
        std::memset(data, 0, sizeof(data));
    }

    // 获取整个包的总大小
    int GetTotalSize() const {
        return sizeof(ProtocolHeader) + header.len;
    }

    // 获取有效载荷大小
    int GetPayloadSize() const {
        return header.len;
    }
};

// ============================================
// 协议参数
// ============================================

// 协议配置结构体
struct ProtocolConfig {
    uint16_t window_size;       // 接收窗口大小（数据包个数）
    uint32_t rto_init;          // 初始RTO（毫秒）
    uint32_t rto_max;           // 最大RTO（毫秒）
    bool enable_sack;           // 是否启用选择确认
    bool enable_congestion_control; // 是否启用拥塞控制

    ProtocolConfig() : window_size(32), rto_init(1000), rto_max(64000),
                       enable_sack(true), enable_congestion_control(true) {}
};

#endif // PROTOCOL_H
