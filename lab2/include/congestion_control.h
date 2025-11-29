#ifndef CONGESTION_CONTROL_H
#define CONGESTION_CONTROL_H

#include <cstdint>
#include <mutex>

// ============================================
// RENO 拥塞控制算法实现
// ============================================

/**
 * RENO拥塞控制算法
 * 支持4个阶段：慢启动、拥塞避免、快重传、快速恢复
 */

class CongestionControl {
public:
    /**
     * 构造函数
     */
    CongestionControl();

    /**
     * 初始化拥塞控制
     * @param mss 最大报文段大小（字节）
     */
    void Initialize(uint32_t mss = 1400);

    /**
     * 处理ACK事件
     * 当收到有效的ACK时调用
     * @param ack_num ACK号
     * @return true表示拥塞窗口有变化
     */
    bool OnAck(uint32_t ack_num);

    /**
     * 处理重复ACK
     * 当收到重复ACK时调用（快重传触发）
     * @param dup_ack_count 重复ACK的个数
     */
    void OnDuplicateAck(int dup_ack_count);

    /**
     * 处理丢包事件（超时重传）
     * 当发生超时重传时调用
     */
    void OnTimeout();

    /**
     * 处理快重传后的新ACK
     * 当收到快重传后的新ACK时调用
     */
    void OnNewAckAfterFastRetransmit();

    /**
     * 获取当前拥塞窗口大小（字节）
     * @return 拥塞窗口大小
     */
    uint32_t GetCongestionWindow() const;

    /**
     * 获取当前拥塞窗口大小（MSS个数）
     * @return 拥塞窗口大小（MSS个数）
     */
    uint32_t GetCongestionWindowMss() const;

    /**
     * 获取慢启动阈值
     * @return ssthresh值（字节）
     */
    uint32_t GetSsthresh() const;

    /**
     * 获取当前拥塞控制状态
     * @return 状态值（0=慢启动, 1=拥塞避免, 2=快速恢复）
     */
    int GetState() const;

    /**
     * 获取当前状态名称
     * @return 状态名称字符串
     */
    const char* GetStateName() const;

    /**
     * 重置拥塞控制
     */
    void Reset();

    /**
     * 获取有效窗口大小（考虑流量控制窗口）
     * @param flow_control_window 流量控制窗口大小
     * @return 有效窗口大小 = min(cwnd, flow_control_wnd)
     */
    uint32_t GetEffectiveWindow(uint32_t flow_control_window) const;

private:
    enum CongestionState {
        STATE_SLOW_START = 0,           // 慢启动
        STATE_CONGESTION_AVOIDANCE = 1, // 拥塞避免
        STATE_FAST_RECOVERY = 2         // 快速恢复
    };

    uint32_t mss_;                      // 最大报文段大小
    uint32_t cwnd_;                     // 拥塞窗口（字节）
    uint32_t ssthresh_;                 // 慢启动阈值（字节）
    CongestionState state_;             // 当前状态
    uint32_t ack_count_;                // 在拥塞避免阶段的ACK计数
    uint32_t fast_recovery_exit_seq_;   // 快速恢复退出时的序列号
    int duplicate_ack_count_;           // 重复ACK计数
    uint32_t last_ack_;                 // 上一次的ACK号
    bool in_fast_recovery_;             // 是否在快速恢复阶段
    mutable std::mutex mutex_;          // 互斥锁（线程安全）

    // 辅助函数
    void EnterSlowStart();
    void EnterCongestionAvoidance();
    void EnterFastRecovery();
    void ExitFastRecovery();
};

#endif // CONGESTION_CONTROL_H
