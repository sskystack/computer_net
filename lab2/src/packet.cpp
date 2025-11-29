#include "packet.h"
#include "common.h"
#include <cstring>
#include <sstream>

// 初始化数据包
void PacketHandler::InitializePacket(Packet* packet, uint8_t flags,
                                    uint32_t seq, uint32_t ack, uint16_t wnd) {
    packet->header.flags = flags;
    packet->header.seq = seq;
    packet->header.ack = ack;
    packet->header.wnd = wnd;
    packet->header.len = 0;
    packet->header.checksum = 0;
    std::memset(packet->data, 0, sizeof(packet->data));
}

// 编码数据包
int PacketHandler::EncodePacket(const Packet* packet, uint8_t* buffer, int buffer_size) {
    int total_size = packet->GetTotalSize();

    if (buffer_size < total_size) {
        LOG_ERROR("PacketHandler", "Buffer too small for encoding");
        return -1;
    }

    // 复制头部
    std::memcpy(buffer, &packet->header, HEADER_SIZE);

    // 复制数据
    if (packet->header.len > 0) {
        std::memcpy(buffer + HEADER_SIZE, packet->data, packet->header.len);
    }

    return total_size;
}

// 解码数据包
int PacketHandler::DecodePacket(const uint8_t* buffer, int buffer_size, Packet* packet) {
    if (buffer_size < HEADER_SIZE) {
        LOG_ERROR("PacketHandler", "Buffer too small for header");
        return -1;
    }

    // 复制头部
    std::memcpy(&packet->header, buffer, HEADER_SIZE);

    int total_size = packet->GetTotalSize();
    if (buffer_size < total_size) {
        LOG_ERROR("PacketHandler", "Buffer too small for complete packet");
        return -1;
    }

    // 复制数据
    if (packet->header.len > 0) {
        std::memcpy(packet->data, buffer + HEADER_SIZE, packet->header.len);
    }

    return total_size;
}

// 创建SYN数据包
void PacketHandler::CreateSynPacket(uint32_t seq, uint16_t wnd, Packet* packet) {
    InitializePacket(packet, FLAG_SYN, seq, 0, wnd);
    CalculateChecksum(packet);
}

// 创建SYN-ACK数据包
void PacketHandler::CreateSynAckPacket(uint32_t seq, uint32_t ack, uint16_t wnd, Packet* packet) {
    InitializePacket(packet, FLAG_SYN | FLAG_ACK, seq, ack, wnd);
    CalculateChecksum(packet);
}

// 创建ACK数据包
void PacketHandler::CreateAckPacket(uint32_t seq, uint32_t ack, uint16_t wnd, Packet* packet) {
    InitializePacket(packet, FLAG_ACK, seq, ack, wnd);
    CalculateChecksum(packet);
}

// 创建FIN数据包
void PacketHandler::CreateFinPacket(uint32_t seq, uint32_t ack, uint16_t wnd, Packet* packet) {
    InitializePacket(packet, FLAG_FIN | FLAG_ACK, seq, ack, wnd);
    CalculateChecksum(packet);
}

// 创建RST数据包
void PacketHandler::CreateRstPacket(uint32_t seq, uint32_t ack, Packet* packet) {
    InitializePacket(packet, FLAG_RST, seq, ack, 0);
    CalculateChecksum(packet);
}

// 创建数据数据包
void PacketHandler::CreateDataPacket(uint32_t seq, uint32_t ack, uint16_t wnd,
                                    const uint8_t* data, int data_len, Packet* packet) {
    if (data_len > DATA_SIZE) {
        LOG_ERROR("PacketHandler", "Data too large");
        data_len = DATA_SIZE;
    }

    InitializePacket(packet, FLAG_DATA | FLAG_ACK, seq, ack, wnd);
    packet->header.len = data_len;
    if (data_len > 0 && data != nullptr) {
        std::memcpy(packet->data, data, data_len);
    }
    CalculateChecksum(packet);
}

// 验证数据包校验和
bool PacketHandler::VerifyChecksum(const Packet* packet) {
    uint32_t stored_checksum = packet->header.checksum;

    // 临时修改校验和为0用于计算
    Packet temp = *packet;
    temp.header.checksum = 0;

    // 计算校验和
    uint32_t calculated = ChecksumCalculator::CalculatePacket(
        (const uint8_t*)&temp.header, HEADER_SIZE,
        temp.data, temp.header.len
    );

    return calculated == stored_checksum;
}

// 计算并填充数据包的校验和
void PacketHandler::CalculateChecksum(Packet* packet) {
    packet->header.checksum = 0;

    uint32_t checksum = ChecksumCalculator::CalculatePacket(
        (const uint8_t*)&packet->header, HEADER_SIZE,
        packet->data, packet->header.len
    );

    packet->header.checksum = checksum;
}

// 打印数据包信息
void PacketHandler::PrintPacketDebug(const std::string& label, const Packet* packet) {
    std::string flags_str;
    if (packet->header.flags & FLAG_SYN) flags_str += "SYN ";
    if (packet->header.flags & FLAG_ACK) flags_str += "ACK ";
    if (packet->header.flags & FLAG_FIN) flags_str += "FIN ";
    if (packet->header.flags & FLAG_RST) flags_str += "RST ";
    if (packet->header.flags & FLAG_DATA) flags_str += "DATA ";

    std::stringstream ss;
    ss << label << " | seq=" << packet->header.seq
       << " ack=" << packet->header.ack
       << " flags=" << flags_str
       << " wnd=" << packet->header.wnd
       << " len=" << packet->header.len
       << " checksum=" << std::hex << packet->header.checksum << std::dec;

    LOG_DEBUG("PacketHandler", ss.str());
}
