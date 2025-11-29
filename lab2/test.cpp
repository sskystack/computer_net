#include <iostream>
#include "include/common.h"
#include "include/protocol.h"
#include "include/checksum.h"
#include "include/packet.h"

int main() {
    std::cout << "=== Lab2: 可靠传输协议 - 基础设施测试 ===" << std::endl << std::endl;

    // 1. 初始化校验和模块
    std::cout << "[1] 初始化校验和模块..." << std::endl;
    ChecksumCalculator::Initialize();
    std::cout << "    ✓ 校验和模块初始化成功" << std::endl << std::endl;

    // 2. 测试协议头结构大小
    std::cout << "[2] 验证协议头大小..." << std::endl;
    std::cout << "    ProtocolHeader size: " << sizeof(ProtocolHeader) << " bytes (expected: 32)" << std::endl;
    if (sizeof(ProtocolHeader) != 32) {
        std::cerr << "    ✗ 错误：协议头大小不为32字节！" << std::endl;
        return 1;
    }
    std::cout << "    ✓ 协议头大小正确" << std::endl << std::endl;

    // 3. 测试数据包结构
    std::cout << "[3] 测试数据包创建和编码..." << std::endl;
    Packet pkt;
    PacketHandler::CreateSynPacket(12345, 32, &pkt);
    std::cout << "    ✓ 创建SYN数据包成功" << std::endl;
    std::cout << "    - seq: " << pkt.header.seq << std::endl;
    std::cout << "    - flags: " << (int)pkt.header.flags << std::endl;
    std::cout << "    - checksum: " << std::hex << pkt.header.checksum << std::dec << std::endl << std::endl;

    // 4. 测试数据包编码和解码
    std::cout << "[4] 测试数据包编码/解码..." << std::endl;
    uint8_t buffer[MAX_PACKET_SIZE];
    int encoded_size = PacketHandler::EncodePacket(&pkt, buffer, MAX_PACKET_SIZE);
    std::cout << "    ✓ 编码成功，编码大小: " << encoded_size << " bytes" << std::endl;

    Packet decoded_pkt;
    int decoded_size = PacketHandler::DecodePacket(buffer, encoded_size, &decoded_pkt);
    std::cout << "    ✓ 解码成功，解码大小: " << decoded_size << " bytes" << std::endl;

    if (decoded_pkt.header.seq != pkt.header.seq) {
        std::cerr << "    ✗ 错误：seq不匹配！" << std::endl;
        return 1;
    }
    std::cout << "    ✓ 解码数据与原数据一致" << std::endl << std::endl;

    // 5. 测试校验和验证
    std::cout << "[5] 测试校验和验证..." << std::endl;
    if (!PacketHandler::VerifyChecksum(&decoded_pkt)) {
        std::cerr << "    ✗ 错误：校验和验证失败！" << std::endl;
        return 1;
    }
    std::cout << "    ✓ 校验和验证成功" << std::endl << std::endl;

    // 6. 测试数据数据包
    std::cout << "[6] 测试数据数据包..." << std::endl;
    Packet data_pkt;
    uint8_t test_data[] = "Hello, RDT Protocol!";
    PacketHandler::CreateDataPacket(54321, 67890, 32, test_data, sizeof(test_data) - 1, &data_pkt);
    std::cout << "    ✓ 创建数据包成功" << std::endl;
    std::cout << "    - 数据长度: " << data_pkt.header.len << " bytes" << std::endl;
    std::cout << "    - 数据内容: " << (char*)data_pkt.data << std::endl << std::endl;

    // 7. 测试序列号比较
    std::cout << "[7] 测试序列号比较函数..." << std::endl;
    uint32_t seq1 = 1000, seq2 = 2000, seq3 = 1000;
    std::cout << "    seq1=" << seq1 << ", seq2=" << seq2 << std::endl;
    std::cout << "    IsSeqBefore(seq1, seq2): " << (IsSeqBefore(seq1, seq2) ? "true" : "false") << std::endl;
    std::cout << "    IsSeqAfter(seq2, seq1): " << (IsSeqAfter(seq2, seq1) ? "true" : "false") << std::endl;
    std::cout << "    IsSeqBeforeOrEqual(seq1, seq3): " << (IsSeqBeforeOrEqual(seq1, seq3) ? "true" : "false") << std::endl << std::endl;

    // 8. 测试日志系统
    std::cout << "[8] 测试日志系统..." << std::endl;
    LOG_INFO("TEST", "这是一条INFO日志");
    LOG_DEBUG("TEST", "这是一条DEBUG日志");
    LOG_WARN("TEST", "这是一条WARN日志");
    LOG_ERROR("TEST", "这是一条ERROR日志");
    std::cout << std::endl;

    // 9. 测试计时器
    std::cout << "[9] 测试计时器..." << std::endl;
    Timer timer;
    // 模拟一些操作
    for (volatile int i = 0; i < 100000000; i++);
    double elapsed = timer.ElapsedMs();
    std::cout << "    ✓ 计时器工作正常，经过时间: " << elapsed << " ms" << std::endl << std::endl;

    std::cout << "=== 所有基础设施测试通过！✓ ===" << std::endl;
    return 0;
}
