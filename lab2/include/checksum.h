#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <cstdint>
#include <cstring>

// ============================================
// 校验和计算和验证
// ============================================

/**
 * 校验和模块
 * 使用CRC32算法进行差错检测
 * 在数据包发送前计算校验和，接收时验证校验和
 */

class ChecksumCalculator {
public:
    /**
     * 初始化CRC32查找表
     * 应在程序启动时调用一次
     */
    static void Initialize();

    /**
     * 计算校验和
     * @param data 数据指针
     * @param length 数据长度
     * @return 计算得到的校验和
     */
    static uint32_t Calculate(const uint8_t* data, int length);

    /**
     * 验证校验和
     * @param data 数据指针
     * @param length 数据长度（包括校验和字段）
     * @param stored_checksum 存储的校验和
     * @return true表示校验成功，false表示校验失败
     */
    static bool Verify(const uint8_t* data, int length, uint32_t stored_checksum);

    /**
     * 计算协议头和数据的校验和
     * @param header_data 协议头数据指针
     * @param header_len 协议头长度
     * @param payload_data 有效载荷数据指针
     * @param payload_len 有效载荷长度
     * @return 计算得到的校验和
     */
    static uint32_t CalculatePacket(const uint8_t* header_data, int header_len,
                                   const uint8_t* payload_data, int payload_len);

private:
    // CRC32查找表
    static uint32_t crc32_table[256];
    static bool initialized;

    // 生成CRC32多项式查表
    static void GenerateCrc32Table();
};

#endif // CHECKSUM_H
