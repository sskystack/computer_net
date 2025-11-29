#include <iostream>
#include "include/common.h"
#include "include/protocol.h"
#include "include/checksum.h"
#include "include/packet.h"
#include "include/window_manager.h"
#include "include/congestion_control.h"

// 打印拥塞控制状态
void PrintCCState(const CongestionControl& cc) {
    std::cout << "    State: " << cc.GetStateName()
              << " | cwnd=" << cc.GetCongestionWindowMss() << " MSS (" << cc.GetCongestionWindow() << " bytes)"
              << " | ssthresh=" << (cc.GetSsthresh() / 1400) << " MSS"
              << std::endl;
}

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
    SendWindow send_window(8);

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
    std::cout << "    ✓ 成功添加5个数据包到发送窗口" << std::endl << std::endl;

    // 4. 接收窗口测试
    std::cout << "[4] 测试接收窗口..." << std::endl;
    ReceiveWindow recv_window(8);

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
    std::cout << "    ✓ 接收窗口正常工作" << std::endl << std::endl;

    // ============ 拥塞控制测试 ============
    std::cout << "--- 第三部分：拥塞控制（RENO）测试 ---" << std::endl << std::endl;

    // 5. 初始化拥塞控制
    std::cout << "[5] 初始化拥塞控制..." << std::endl;
    CongestionControl cc;
    cc.Initialize(1400);  // MSS = 1400 bytes
    std::cout << "    ✓ 拥塞控制初始化成功" << std::endl;
    PrintCCState(cc);
    std::cout << std::endl;

    // 6. 模拟慢启动阶段
    std::cout << "[6] 模拟慢启动阶段..." << std::endl;
    std::cout << "    在慢启动阶段，每收到一个ACK，cwnd增加1 MSS" << std::endl;
    for (int i = 0; i < 10; i++) {
        cc.OnAck(1000 + i);
        std::cout << "    ACK #" << (i+1) << ": ";
        PrintCCState(cc);
    }
    std::cout << std::endl;

    // 7. 拥塞避免阶段
    std::cout << "[7] 进入拥塞避免阶段..." << std::endl;
    std::cout << "    继续发送ACK，观察窗口增长速度下降" << std::endl;
    uint32_t prev_cwnd = cc.GetCongestionWindow();
    for (int i = 0; i < 30; i++) {
        cc.OnAck(1010 + i);
        uint32_t curr_cwnd = cc.GetCongestionWindow();
        if (curr_cwnd > prev_cwnd) {
            std::cout << "    ACK #" << (10+i+1) << ": ";
            PrintCCState(cc);
            prev_cwnd = curr_cwnd;
        }
    }
    std::cout << std::endl;

    // 8. 超时重传（回到慢启动）
    std::cout << "[8] 模拟超时重传..." << std::endl;
    uint32_t cwnd_before_timeout = cc.GetCongestionWindow();
    std::cout << "    超时前 cwnd: " << (cwnd_before_timeout / 1400) << " MSS" << std::endl;
    cc.OnTimeout();
    std::cout << "    超时后 ";
    PrintCCState(cc);
    std::cout << std::endl;

    // 9. 快重传和快速恢复
    std::cout << "[9] 模拟快重传和快速恢复..." << std::endl;
    CongestionControl cc2;
    cc2.Initialize(1400);

    // 先让窗口增长到一定大小
    std::cout << "    第1步：让cwnd增长到16 MSS（进入拥塞避免）..." << std::endl;
    for (int i = 0; i < 100; i++) {
        cc2.OnAck(2000 + i);
        if (cc2.GetCongestionWindowMss() >= 16) {
            break;
        }
    }
    std::cout << "    当前 ";
    PrintCCState(cc2);

    // 模拟3个重复ACK（丢包）
    std::cout << "    第2步：收到3个重复ACK（触发快重传）..." << std::endl;
    uint32_t last_ack = 2100;
    cc2.OnAck(last_ack);  // 新的ACK
    cc2.OnAck(last_ack);  // 重复ACK 1
    cc2.OnAck(last_ack);  // 重复ACK 2
    cc2.OnAck(last_ack);  // 重复ACK 3（触发快重传）
    std::cout << "    进入快速恢复后 ";
    PrintCCState(cc2);

    // 收到新的ACK，退出快速恢复
    std::cout << "    第3步：收到新的ACK，退出快速恢复..." << std::endl;
    cc2.OnAck(2101);
    std::cout << "    退出快速恢复后 ";
    PrintCCState(cc2);
    std::cout << std::endl;

    // 10. 窗口清空
    std::cout << "[10] 测试拥塞控制重置..." << std::endl;
    cc.Reset();
    std::cout << "    重置后 ";
    PrintCCState(cc);
    std::cout << std::endl;

    std::cout << "=== 所有测试通过！✓ ===" << std::endl;
    return 0;
}
