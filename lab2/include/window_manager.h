#ifndef WINDOW_MANAGER_H
#define WINDOW_MANAGER_H

#include "protocol.h"
#include "common.h"
#include <queue>
#include <map>
#include <mutex>
#include <memory>

// ============================================
// 发送窗口和接收窗口管理
// ============================================

/**
 * 发送窗口数据包结构
 * 用于存储待发送或已发送但未确认的数据包
 */
struct SendWindowPacket {
    Packet packet;              // 数据包内容
    uint32_t seq;               // 序列号
    long long send_time;        // 发送时间（毫秒时间戳）
    int retransmit_count;       // 重传次数
    bool acked;                 // 是否已被确认

    SendWindowPacket() : seq(0), send_time(0), retransmit_count(0), acked(false) {}
};

/**
 * 接收窗口数据包结构
 * 用于存储乱序到达的数据包
 */
struct ReceiveWindowPacket {
    Packet packet;              // 数据包内容
    uint32_t seq;               // 序列号
    bool received;              // 是否已接收

    ReceiveWindowPacket() : seq(0), received(false) {}
};

// ============================================
// 发送窗口类
// ============================================

class SendWindow {
public:
    /**
     * 构造函数
     * @param window_size 窗口大小（数据包个数）
     */
    explicit SendWindow(uint16_t window_size = DEFAULT_WINDOW_SIZE);

    /**
     * 添加数据到发送窗口
     * @param packet 数据包指针
     * @param seq 序列号
     * @return true表示成功，false表示窗口已满
     */
    bool AddPacket(const Packet* packet, uint32_t seq);

    /**
     * 检查是否可以发送数据包
     * @param seq 序列号
     * @return true表示可以发送
     */
    bool CanSend(uint32_t seq) const;

    /**
     * 确认数据包（标记为已确认）
     * @param seq 序列号
     * @return true表示成功，false表示序列号不存在
     */
    bool AckPacket(uint32_t seq);

    /**
     * 获取下一个待发送的数据包
     * @param out_packet 输出数据包指针
     * @param out_seq 输出序列号指针
     * @return true表示找到，false表示没有待发送的包
     */
    bool GetNextPacket(Packet* out_packet, uint32_t* out_seq);

    /**
     * 获取需要重传的数据包
     * @param timeout_ms 超时时间（毫秒）
     * @param out_packet 输出数据包指针
     * @param out_seq 输出序列号指针
     * @return true表示找到需要重传的包
     */
    bool GetRetransmitPacket(long long timeout_ms, Packet* out_packet, uint32_t* out_seq);

    /**
     * 获取当前窗口内已确认的最大连续序列号
     * @return 最大连续确认序列号
     */
    uint32_t GetAckSeq() const;

    /**
     * 滑动窗口（移除已确认的包）
     */
    void SlideWindow();

    /**
     * 获取窗口中未确认的包数量
     * @return 包数量
     */
    int GetUnackedCount() const;

    /**
     * 获取窗口是否已满
     * @return true表示已满
     */
    bool IsWindowFull() const;

    /**
     * 获取窗口大小
     * @return 窗口大小
     */
    uint16_t GetWindowSize() const { return window_size_; }

    /**
     * 清空窗口
     */
    void Clear();

private:
    uint16_t window_size_;                          // 窗口大小
    std::map<uint32_t, SendWindowPacket> packets_;  // 存储的数据包（seq -> packet）
    uint32_t base_seq_;                             // 窗口基序列号
    uint32_t next_seq_;                             // 下一个待发送序列号
    mutable std::mutex mutex_;                      // 互斥锁（线程安全）

    // 辅助函数
    bool IsSeqInWindow(uint32_t seq) const;
};

// ============================================
// 接收窗口类
// ============================================

class ReceiveWindow {
public:
    /**
     * 构造函数
     * @param window_size 窗口大小（数据包个数）
     */
    explicit ReceiveWindow(uint16_t window_size = DEFAULT_WINDOW_SIZE);

    /**
     * 向接收窗口添加数据包
     * @param packet 数据包指针
     * @param seq 序列号
     * @return true表示成功添加，false表示重复或窗口已满
     */
    bool AddPacket(const Packet* packet, uint32_t seq);

    /**
     * 获取下一个可以交付的数据包
     * @param out_packet 输出数据包指针
     * @param out_seq 输出序列号指针
     * @return true表示找到可交付的包
     */
    bool GetNextDeliverable(Packet* out_packet, uint32_t* out_seq);

    /**
     * 检查是否已接收指定序列号的数据包
     * @param seq 序列号
     * @return true表示已接收
     */
    bool IsPacketReceived(uint32_t seq) const;

    /**
     * 获取期望接收的序列号（下一个应该接收的）
     * @return 期望序列号
     */
    uint32_t GetExpectedSeq() const;

    /**
     * 获取窗口中缓存的包数量
     * @return 包数量
     */
    int GetBufferedCount() const;

    /**
     * 获取窗口大小
     * @return 窗口大小
     */
    uint16_t GetWindowSize() const { return window_size_; }

    /**
     * 清空窗口
     */
    void Clear();

private:
    uint16_t window_size_;                              // 窗口大小
    std::map<uint32_t, ReceiveWindowPacket> packets_;   // 存储的数据包
    uint32_t expected_seq_;                             // 期望接收的序列号
    mutable std::mutex mutex_;                          // 互斥锁（线程安全）

    // 辅助函数
    bool IsSeqInWindow(uint32_t seq) const;
};

#endif // WINDOW_MANAGER_H
