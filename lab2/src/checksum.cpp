#include "checksum.h"

// 静态成员初始化
uint32_t ChecksumCalculator::crc32_table[256] = {0};
bool ChecksumCalculator::initialized = false;

// CRC32多项式
#define CRC32_POLYNOMIAL 0xEDB88320

// 初始化CRC32查找表
void ChecksumCalculator::Initialize() {
    if (initialized) {
        return;
    }
    GenerateCrc32Table();
    initialized = true;
}

// 生成CRC32查查表
void ChecksumCalculator::GenerateCrc32Table() {
    for (int i = 0; i < 256; i++) {
        uint32_t crc = i;
        for (int j = 0; j < 8; j++) {
            if (crc & 1) {
                crc = (crc >> 1) ^ CRC32_POLYNOMIAL;
            } else {
                crc = crc >> 1;
            }
        }
        crc32_table[i] = crc;
    }
}

// 计算校验和
uint32_t ChecksumCalculator::Calculate(const uint8_t* data, int length) {
    if (!initialized) {
        Initialize();
    }

    uint32_t crc = 0xFFFFFFFF;  // 初始值
    for (int i = 0; i < length; i++) {
        uint8_t byte = data[i];
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFF;  // 最终异或
}

// 验证校验和
bool ChecksumCalculator::Verify(const uint8_t* data, int length,
                                uint32_t stored_checksum) {
    if (!initialized) {
        Initialize();
    }

    uint32_t calculated = Calculate(data, length);
    return calculated == stored_checksum;
}

// 计算协议头和数据的校验和
uint32_t ChecksumCalculator::CalculatePacket(const uint8_t* header_data, int header_len,
                                           const uint8_t* payload_data, int payload_len) {
    if (!initialized) {
        Initialize();
    }

    uint32_t crc = 0xFFFFFFFF;

    // 计算头部校验和
    for (int i = 0; i < header_len; i++) {
        uint8_t byte = header_data[i];
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }

    // 计算有效载荷校验和
    for (int i = 0; i < payload_len; i++) {
        uint8_t byte = payload_data[i];
        crc = crc32_table[(crc ^ byte) & 0xFF] ^ (crc >> 8);
    }

    return crc ^ 0xFFFFFFFF;
}
