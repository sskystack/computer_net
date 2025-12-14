#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>

// 协议常量定义
const uint16_t PACKET_SIZE = 1024;           // 数据包大小（包括头部）
const uint16_t DATA_SIZE = PACKET_SIZE - 64; // 实际数据大小 = 1024 - 64字节头
const uint16_t WINDOW_SIZE = 10;             // 滑动窗口大小（固定）
const uint32_t TIMEOUT_MS = 500;             // 超时时间（毫秒）
const uint32_t CONNECT_TIMEOUT_MS = 5000;   // 连接超时时间

// RENO拥塞控制状态
enum CongestionState {
    SLOW_START,              // 慢启动
    CONGESTION_AVOIDANCE,    // 拥塞避免
    FAST_RECOVERY           // 快速恢复
};

// 数据包类型
enum PacketType {
    PKT_SYN = 0,       // 连接请求
    PKT_SYN_ACK = 1,   // 连接应答
    PKT_ACK = 2,       // 确认包
    PKT_DATA = 3,      // 数据包
    PKT_FIN = 4,       // 结束连接
    PKT_FIN_ACK = 5    // 结束确认
};

// 数据包头结构体（64字节）
struct PacketHeader {
    uint32_t seq_num;           // 序列号 (4字节)
    uint32_t ack_num;           // 确认号 (4字节)
    uint16_t packet_type;       // 包类型 (2字节)
    uint16_t data_length;       // 数据长度 (2字节)
    uint32_t checksum;          // 校验和 (4字节)
    uint32_t file_size;         // 文件大小（仅在首个SYN或DATA包中有效）(4字节)
    char filename[32];          // 文件名（仅在首个SYN中有效）(32字节)
    uint8_t reserved[6];        // 保留字段 (6字节)

    PacketHeader() {
        memset(this, 0, sizeof(PacketHeader));
    }
};

// 完整数据包结构
struct Packet {
    PacketHeader header;
    char data[DATA_SIZE];

    Packet() {
        header = PacketHeader();
        memset(data, 0, DATA_SIZE);
    }
};

// 计算校验和（16位反码和校验）
inline uint32_t calculateChecksum(const void* buffer, size_t length) {
    uint32_t sum = 0;
    const uint8_t* data = (const uint8_t*)buffer;

    // 将所有字节相加
    for (size_t i = 0; i < length; i++) {
        sum += data[i];
    }

    // 处理进位（16位折叠）
    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    // 返回反码
    return (~sum) & 0xFFFF;
}

// 验证校验和
inline bool verifyChecksum(const void* buffer, size_t length, uint32_t received_checksum) {
    uint32_t calculated = calculateChecksum(buffer, length);
    return calculated == received_checksum;
}

#endif // PROTOCOL_H
