#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <cstdint>
#include <cstring>
#include <winsock2.h>

// 协议常量定义
const uint16_t PACKET_SIZE = 1024;           // 数据包大小（包括头部）
const uint16_t DATA_SIZE = PACKET_SIZE - 64; // 实际数据大小 = 1024 - 64字节头
const uint16_t WINDOW_SIZE = 10;             // 滑动窗口大小（固定）
const uint32_t TIMEOUT_MS = 500;             // 超时时间（毫秒）
const uint32_t CONNECT_TIMEOUT_MS = 5000;   // 连接超时时间

// SACK相关常量
const uint8_t MAX_SACK_BLOCKS = 10;          // 最多SACK块数量
const uint16_t SACK_BLOCK_SIZE = 8;          // 每个SACK块大小（4字节start + 4字节end）

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

// ===== SACK块编码/解码函数 =====

// SACK块结构
struct SackBlock {
    uint32_t start;
    uint32_t end;
};

// 将SACK块编码到ACK包的data部分
// 返回编码后的字节数
inline uint16_t encodeSackBlocks(const SackBlock* blocks, uint8_t count, char* data, uint16_t max_len) {
    if (count == 0) return 0;
    if (count > MAX_SACK_BLOCKS) count = MAX_SACK_BLOCKS;

    uint8_t* ptr = (uint8_t*)data;

    // 第一字节存SACK块数量
    ptr[0] = count;
    uint16_t offset = 1;

    // 编码每个SACK块
    for (uint8_t i = 0; i < count && offset + SACK_BLOCK_SIZE <= max_len; i++) {
        // 编码start（网络字节序）
        uint32_t start = htonl(blocks[i].start);
        memcpy(&ptr[offset], &start, 4);
        offset += 4;

        // 编码end（网络字节序）
        uint32_t end = htonl(blocks[i].end);
        memcpy(&ptr[offset], &end, 4);
        offset += 4;
    }

    return offset;
}

// 从ACK包的data部分解码SACK块
// 返回解码的SACK块数量
inline uint8_t decodeSackBlocks(const char* data, uint16_t data_len, SackBlock* blocks, uint8_t max_blocks) {
    if (data_len < 1) return 0;

    const uint8_t* ptr = (const uint8_t*)data;
    uint8_t count = ptr[0];

    if (count == 0 || count > max_blocks) return 0;

    uint16_t offset = 1;
    for (uint8_t i = 0; i < count; i++) {
        if (offset + SACK_BLOCK_SIZE > data_len) break;

        // 解码start（网络字节序）
        uint32_t start;
        memcpy(&start, &ptr[offset], 4);
        blocks[i].start = ntohl(start);
        offset += 4;

        // 解码end（网络字节序）
        uint32_t end;
        memcpy(&end, &ptr[offset], 4);
        blocks[i].end = ntohl(end);
        offset += 4;
    }

    return count;
}

#endif // PROTOCOL_H
