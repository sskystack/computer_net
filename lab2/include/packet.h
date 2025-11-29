#ifndef PACKET_H
#define PACKET_H

#include "protocol.h"
#include "checksum.h"
#include <cstdint>
#include <cstring>

// ============================================
// 数据包编码和解码
// ============================================

/**
 * 数据包处理模块
 * 负责数据包的序列化、反序列化、编码、解码等操作
 */

class PacketHandler {
public:
    /**
     * 编码数据包：将ProtocolHeader和数据编码为字节流
     * @param packet 源数据包指针
     * @param buffer 目标缓冲区指针
     * @param buffer_size 缓冲区大小
     * @return 编码后的字节数，失败返回-1
     */
    static int EncodePacket(const Packet* packet, uint8_t* buffer, int buffer_size);

    /**
     * 解码数据包：从字节流解码为ProtocolHeader和数据
     * @param buffer 源缓冲区指针
     * @param buffer_size 缓冲区大小
     * @param packet 目标数据包指针
     * @return 解码的字节数，失败返回-1
     */
    static int DecodePacket(const uint8_t* buffer, int buffer_size, Packet* packet);

    /**
     * 创建SYN数据包
     * @param seq 序列号
     * @param wnd 接收窗口大小
     * @param packet 目标数据包
     */
    static void CreateSynPacket(uint32_t seq, uint16_t wnd, Packet* packet);

    /**
     * 创建SYN-ACK数据包
     * @param seq 序列号
     * @param ack 确认号
     * @param wnd 接收窗口大小
     * @param packet 目标数据包
     */
    static void CreateSynAckPacket(uint32_t seq, uint32_t ack, uint16_t wnd, Packet* packet);

    /**
     * 创建ACK数据包
     * @param seq 序列号
     * @param ack 确认号
     * @param wnd 接收窗口大小
     * @param packet 目标数据包
     */
    static void CreateAckPacket(uint32_t seq, uint32_t ack, uint16_t wnd, Packet* packet);

    /**
     * 创建FIN数据包
     * @param seq 序列号
     * @param ack 确认号
     * @param wnd 接收窗口大小
     * @param packet 目标数据包
     */
    static void CreateFinPacket(uint32_t seq, uint32_t ack, uint16_t wnd, Packet* packet);

    /**
     * 创建RST数据包
     * @param seq 序列号
     * @param ack 确认号
     * @param packet 目标数据包
     */
    static void CreateRstPacket(uint32_t seq, uint32_t ack, Packet* packet);

    /**
     * 创建数据数据包
     * @param seq 序列号
     * @param ack 确认号
     * @param wnd 接收窗口大小
     * @param data 数据指针
     * @param data_len 数据长度
     * @param packet 目标数据包
     */
    static void CreateDataPacket(uint32_t seq, uint32_t ack, uint16_t wnd,
                                const uint8_t* data, int data_len, Packet* packet);

    /**
     * 验证数据包校验和
     * @param packet 数据包指针
     * @return true表示校验成功，false表示失败
     */
    static bool VerifyChecksum(const Packet* packet);

    /**
     * 计算并填充数据包的校验和
     * @param packet 数据包指针
     */
    static void CalculateChecksum(Packet* packet);

    /**
     * 打印数据包信息（用于调试）
     * @param label 标签字符串
     * @param packet 数据包指针
     */
    static void PrintPacketDebug(const std::string& label, const Packet* packet);

private:
    // 内部辅助函数
    static void InitializePacket(Packet* packet, uint8_t flags,
                                uint32_t seq, uint32_t ack, uint16_t wnd);
};

#endif // PACKET_H
