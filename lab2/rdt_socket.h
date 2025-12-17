#ifndef RDT_SOCKET_H
#define RDT_SOCKET_H

#include "protocol.h"
#include <winsock2.h>
#include <queue>
#include <map>
#include <chrono>
#include <set>

// 发送窗口中的包信息
struct SendWindowEntry {
    Packet packet;
    std::chrono::steady_clock::time_point send_time;
    uint32_t retransmit_count;
};

class RdtSocket {
public:
    // 构造和析构
    RdtSocket();
    ~RdtSocket();

    // 连接管理
    bool bind(const char* ip, uint16_t port);
    bool connect(const char* ip, uint16_t port);
    bool listen(uint16_t port);
    RdtSocket* accept();
    bool close();

    // 发送和接收
    int sendData(const void* data, size_t length);
    int recvData(void* buffer, size_t max_length);

    // 文件传输
    bool sendFile(const char* filename);
    bool recvFile(const char* save_path);

    // 状态查询
    bool isConnected() const { return connected; }
    SOCKET getRawSocket() const { return sock; }
    uint32_t getLocalSeq() const { return local_seq; }
    uint32_t getRemoteSeq() const { return remote_seq; }

private:
    // Socket相关
    SOCKET sock;
    sockaddr_in local_addr;
    sockaddr_in remote_addr;
    bool connected;

    // 序列号和确认号
    uint32_t local_seq;          // 本地发送的下一个序列号
    uint32_t remote_seq;         // 远程发送的序列号
    uint32_t recv_base;          // 期望接收的下一个序列号

    // ===== 发送窗口管理（流水线 + 选择确认） =====
    uint32_t send_base;                              // 发送窗口的基序号（已确认的最高序号）
    std::map<uint32_t, SendWindowEntry> send_window; // 发送窗口中的包
    std::set<uint32_t> acked_packets;                // 已确认的包序号（用于选择确认）

    // ===== 接收缓冲区（支持乱序接收） =====
    std::map<uint32_t, Packet> recv_buffer;          // 接收缓冲区（用于乱序数据）

    // ===== 发送端SACK追踪 =====
    std::set<uint32_t> sacked_packets;               // 通过SACK确认的包序号（不连续的部分）

    // ===== 拥塞控制（RENO算法） =====
    CongestionState cong_state;   // 拥塞控制状态
    uint32_t cwnd;                 // 拥塞窗口大小（以包数计）
    uint32_t ssthresh;             // 慢启动阈值
    uint32_t dup_ack_count;        // 重复ACK计数
    uint32_t last_ack_seq;         // 上次ACK的序列号
    uint32_t ca_acc;               // 拥塞避免累加器（定点数实现）

    // 辅助函数
    bool sendPacket(const Packet& pkt);
    bool recvPacket(Packet& pkt, uint32_t timeout_ms = TIMEOUT_MS);

    // 窗口管理相关
    uint32_t getEffectiveWindow();              // 获取有效发送窗口（考虑拥塞控制）
    bool canSendPacket();                       // 检查是否可以发送包
    void slideWindow(uint32_t ack_seq);         // 发送窗口前进
    bool isPacketInWindow(uint32_t seq);        // 检查包是否在接收窗口内
    void processAck(uint32_t ack_seq);          // 处理ACK包

    // 重传相关
    void retransmitPackets();                   // 检查超时并重传
    bool isTimerExpired(uint32_t seq);          // 检查计时器是否超时

    // 拥塞控制相关（RENO）
    void onNewAck();                            // 收到新的ACK
    void onDuplicateAck();                      // 收到重复ACK
    void onTimeout();                           // 超时事件
    void updateCongestionWindow();              // 更新拥塞窗口

    // 连接建立
    bool sendSyn();
    bool sendSynAck();
    bool sendFin();
    bool sendFinAck();
    bool sendAck(uint32_t ack_seq);            // 发送ACK包
    bool sendAckWithSack(uint32_t ack_seq);    // 发送带SACK块的ACK包

    // SACK相关
    void generateSackBlocks(SackBlock* blocks, uint8_t& count);  // 从recv_buffer生成SACK块

    // 日志输出
    void log(const char* format, ...);
    void logPacketHeader(const char* label, const PacketHeader& header);
};

#endif // RDT_SOCKET_H
