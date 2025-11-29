#include <iostream>
#include "include/common.h"
#include "include/protocol.h"
#include "include/checksum.h"
#include "include/packet.h"
#include "include/window_manager.h"

int main() {
    std::cout << "=== Lab2: 可靠传输协议 - 完整测试 ===" << std::endl << std::endl;

    // 初始化校验和
    ChecksumCalculator::Initialize();

    // ============ 基础设施测试 ============
    std::cout << "--- 第一部分：基础设施测试 ---" << std::endl << std::endl;

    // 1. 协议头大小
    std::cout << "[1] 验证协议头大小..." << std::endl;
    if (sizeof(ProtocolHeader) != 32) {
        std::cerr << "    ✗ 错误：协议头大小不为32字节！" << std::endl;
        return 1;
    }
    std::cout << "    ✓ 协议头大小正确（32字节）" << std::endl << std::endl;

    // 2. 数据包操作
    std::cout << "[2] 测试数据包编码/解码..." << std::endl;
    Packet pkt;
    PacketHandler::CreateSynPacket(12345, 32, &pkt);
    uint8_t buffer[MAX_PACKET_SIZE];
    int encoded_size = PacketHandler::EncodePacket(&pkt, buffer, MAX_PACKET_SIZE);
    Packet decoded_pkt;
    PacketHandler::DecodePacket(buffer, encoded_size, &decoded_pkt);
    if (!PacketHandler::VerifyChecksum(&decoded_pkt)) {
        std::cerr << "    ✗ 错误：校验和验证失败！" << std::endl;
        return 1;
    }
    std::cout << "    ✓ 数据包编码/解码/校验和验证成功" << std::endl << std::endl;

    // ============ 窗口管理测试 ============
    std::cout << "--- 第二部分：窗口管理测试 ---" << std::endl << std::endl;

    // 3. 发送窗口测试
    std::cout << "[3] 测试发送窗口..." << std::endl;
    SendWindow send_window(8);  // 8个包的窗口

    // 添加多个数据包
    for (int i = 0; i < 5; i++) {
        Packet data_pkt;
        uint8_t test_data[50];
        sprintf((char*)test_data, "Packet %d", i);
        PacketHandler::CreateDataPacket(1000 + i, 0, 32, test_data, strlen((char*)test_data), &data_pkt);
        if (!send_window.AddPacket(&data_pkt, 1000 + i)) {
            std::cerr << "    ✗ 错误：添加数据包失败！" << std::endl;
            return 1;
        }
    }
    std::cout << "    ✓ 成功添加5个数据包到发送窗口" << std::endl;
    std::cout << "    - 未确认包数: " << send_window.GetUnackedCount() << std::endl;
    std::cout << "    - 窗口是否满: " << (send_window.IsWindowFull() ? "是" : "否") << std::endl << std::endl;

    // 4. 确认数据包
    std::cout << "[4] 测试数据包确认..." << std::endl;
    if (!send_window.AckPacket(1000)) {
        std::cerr << "    ✗ 错误：确认数据包失败！" << std::endl;
        return 1;
    }
    std::cout << "    ✓ 成功确认序列号为1000的数据包" << std::endl;
    std::cout << "    - 未确认包数: " << send_window.GetUnackedCount() << std::endl << std::endl;

    // 5. 接收窗口测试
    std::cout << "[5] 测试接收窗口..." << std::endl;
    ReceiveWindow recv_window(8);

    // 乱序接收数据包（模拟丢包和乱序）
    std::cout << "    添加乱序数据包..." << std::endl;
    // 添加第2、3、5个包（缺少第0、1、4个包）。期望序列号从0开始
    for (int seq : {2, 3, 5}) {
        Packet data_pkt;
        uint8_t test_data[50];
        sprintf((char*)test_data, "Packet %d", seq);
        PacketHandler::CreateDataPacket(seq, 0, 32, test_data, strlen((char*)test_data), &data_pkt);
        if (!recv_window.AddPacket(&data_pkt, seq)) {
            std::cerr << "    ✗ 错误：添加接收窗口数据包失败！" << std::endl;
            return 1;
        }
    }
    std::cout << "    ✓ 成功添加乱序数据包（缺少0和1）" << std::endl;
    std::cout << "    - 缓冲包数: " << recv_window.GetBufferedCount() << std::endl;
    std::cout << "    - 期望序列号: " << recv_window.GetExpectedSeq() << std::endl << std::endl;

    // 6. 测试有序传递
    std::cout << "[6] 测试有序传递..." << std::endl;
    std::cout << "    尝试提取包（应该没有，因为缺少第一个包）..." << std::endl;
    Packet out_pkt;
    uint32_t out_seq;
    if (recv_window.GetNextDeliverable(&out_pkt, &out_seq)) {
        std::cerr << "    ✗ 错误：不应该提取包！" << std::endl;
        return 1;
    }
    std::cout << "    ✓ 正确：没有可交付的包（等待序列号0）" << std::endl << std::endl;

    // 7. 添加缺失的包
    std::cout << "[7] 添加缺失的数据包..." << std::endl;
    Packet missing_pkt;
    uint8_t missing_data[50];
    sprintf((char*)missing_data, "Packet 0");
    PacketHandler::CreateDataPacket(0, 0, 32, missing_data, strlen((char*)missing_data), &missing_pkt);
    if (!recv_window.AddPacket(&missing_pkt, 0)) {
        std::cerr << "    ✗ 错误：添加缺失包失败！" << std::endl;
        return 1;
    }
    std::cout << "    ✓ 成功添加序列号0的包" << std::endl << std::endl;

    // 8. 现在应该能提取包
    std::cout << "[8] 提取有序的包..." << std::endl;
    int delivered_count = 0;
    while (recv_window.GetNextDeliverable(&out_pkt, &out_seq)) {
        std::cout << "    ✓ 提取包: seq=" << out_seq << " data=" << (char*)out_pkt.data << std::endl;
        delivered_count++;
    }
    std::cout << "    总共提取了 " << delivered_count << " 个包" << std::endl;
    std::cout << "    - 缓冲包数: " << recv_window.GetBufferedCount() << std::endl;
    std::cout << "    - 期望序列号: " << recv_window.GetExpectedSeq() << std::endl << std::endl;

    // 9. 窗口清空
    std::cout << "[9] 测试窗口清空..." << std::endl;
    send_window.Clear();
    recv_window.Clear();
    std::cout << "    ✓ 窗口已清空" << std::endl;
    std::cout << "    - 发送窗口未确认包数: " << send_window.GetUnackedCount() << std::endl;
    std::cout << "    - 接收窗口缓冲包数: " << recv_window.GetBufferedCount() << std::endl << std::endl;

    std::cout << "=== 所有测试通过！✓ ===" << std::endl;
    return 0;
}
