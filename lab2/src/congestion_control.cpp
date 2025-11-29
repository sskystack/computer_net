#include "congestion_control.h"
#include "common.h"
#include <algorithm>

CongestionControl::CongestionControl()
    : mss_(1400), cwnd_(0), ssthresh_(0), state_(STATE_SLOW_START),
      ack_count_(0), fast_recovery_exit_seq_(0), duplicate_ack_count_(0),
      last_ack_(0), in_fast_recovery_(false) {}

void CongestionControl::Initialize(uint32_t mss) {
    std::lock_guard<std::mutex> lock(mutex_);

    mss_ = mss;
    cwnd_ = mss_;  // 初始拥塞窗口 = 1 MSS
    ssthresh_ = 16 * mss_;  // 初始阈值 = 16 MSS
    state_ = STATE_SLOW_START;
    ack_count_ = 0;
    duplicate_ack_count_ = 0;
    last_ack_ = 0;
    in_fast_recovery_ = false;

    LOG_INFO("CongestionControl", "Initialized: mss=" + std::to_string(mss) +
             " cwnd=" + std::to_string(cwnd_) + " ssthresh=" + std::to_string(ssthresh_));
}

bool CongestionControl::OnAck(uint32_t ack_num) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 新的ACK，重置重复ACK计数
    if (ack_num > last_ack_) {
        duplicate_ack_count_ = 0;
        last_ack_ = ack_num;

        // 如果在快速恢复中，收到新ACK则退出
        if (in_fast_recovery_) {
            LOG_INFO("CongestionControl", "Exiting fast recovery after new ACK");
            ExitFastRecovery();
        }
    } else if (ack_num == last_ack_) {
        // 重复ACK
        duplicate_ack_count_++;
        if (duplicate_ack_count_ == 3 && !in_fast_recovery_) {
            // 3个重复ACK，触发快重传
            LOG_WARN("CongestionControl", "3 duplicate ACKs received, entering fast retransmit");
            EnterFastRecovery();
            return true;
        }
        return false;
    }

    // 处理新的ACK
    bool window_changed = false;

    if (state_ == STATE_SLOW_START) {
        // 慢启动：每个ACK使cwnd增加1 MSS
        cwnd_ += mss_;
        LOG_DEBUG("CongestionControl", "Slow Start: cwnd=" + std::to_string(cwnd_) +
                  " (increased by " + std::to_string(mss_) + ")");

        // 检查是否达到阈值
        if (cwnd_ >= ssthresh_) {
            LOG_INFO("CongestionControl", "Slow Start threshold reached, entering Congestion Avoidance");
            EnterCongestionAvoidance();
        }
        window_changed = true;

    } else if (state_ == STATE_CONGESTION_AVOIDANCE) {
        // 拥塞避免：每RTT增加1 MSS
        // 简化实现：每收到cwnd/mss个ACK增加1 MSS
        ack_count_++;
        uint32_t acks_per_window = cwnd_ / mss_;
        if (ack_count_ >= acks_per_window) {
            cwnd_ += mss_;
            ack_count_ = 0;
            LOG_DEBUG("CongestionControl", "Congestion Avoidance: cwnd=" + std::to_string(cwnd_));
            window_changed = true;
        }
    }

    return window_changed;
}

void CongestionControl::OnDuplicateAck(int dup_ack_count) {
    std::lock_guard<std::mutex> lock(mutex_);

    duplicate_ack_count_ = dup_ack_count;

    if (dup_ack_count >= 3 && !in_fast_recovery_) {
        LOG_WARN("CongestionControl", "Multiple duplicate ACKs detected");
        EnterFastRecovery();
    }
}

void CongestionControl::OnTimeout() {
    std::lock_guard<std::mutex> lock(mutex_);

    LOG_WARN("CongestionControl", "Timeout occurred, cwnd=" + std::to_string(cwnd_) +
             " ssthresh=" + std::to_string(ssthresh_));

    // 超时：
    // 1. ssthresh = cwnd / 2
    // 2. cwnd = 1 MSS
    // 3. 进入慢启动

    ssthresh_ = std::max(2 * mss_, cwnd_ / 2);
    cwnd_ = mss_;
    duplicate_ack_count_ = 0;
    in_fast_recovery_ = false;
    ack_count_ = 0;

    EnterSlowStart();

    LOG_INFO("CongestionControl", "After timeout: ssthresh=" + std::to_string(ssthresh_) +
             " cwnd=" + std::to_string(cwnd_));
}

void CongestionControl::OnNewAckAfterFastRetransmit() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!in_fast_recovery_) {
        return;
    }

    // 在快速恢复中，对于部分ACK，cwnd增加1 MSS
    cwnd_ += mss_;
    LOG_DEBUG("CongestionControl", "Fast Recovery: cwnd=" + std::to_string(cwnd_) +
              " (partial ACK)");
}

uint32_t CongestionControl::GetCongestionWindow() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cwnd_;
}

uint32_t CongestionControl::GetCongestionWindowMss() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cwnd_ / mss_;
}

uint32_t CongestionControl::GetSsthresh() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return ssthresh_;
}

int CongestionControl::GetState() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return (int)state_;
}

const char* CongestionControl::GetStateName() const {
    std::lock_guard<std::mutex> lock(mutex_);
    switch (state_) {
        case STATE_SLOW_START:
            return "Slow Start";
        case STATE_CONGESTION_AVOIDANCE:
            return "Congestion Avoidance";
        case STATE_FAST_RECOVERY:
            return "Fast Recovery";
        default:
            return "Unknown";
    }
}

void CongestionControl::Reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    cwnd_ = mss_;
    ssthresh_ = 16 * mss_;
    state_ = STATE_SLOW_START;
    ack_count_ = 0;
    duplicate_ack_count_ = 0;
    last_ack_ = 0;
    in_fast_recovery_ = false;
    LOG_INFO("CongestionControl", "Reset to initial state");
}

uint32_t CongestionControl::GetEffectiveWindow(uint32_t flow_control_window) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return std::min(cwnd_, flow_control_window);
}

// ============================================
// 私有辅助函数
// ============================================

void CongestionControl::EnterSlowStart() {
    state_ = STATE_SLOW_START;
    ack_count_ = 0;
    LOG_DEBUG("CongestionControl", "Entering Slow Start state");
}

void CongestionControl::EnterCongestionAvoidance() {
    state_ = STATE_CONGESTION_AVOIDANCE;
    ack_count_ = 0;
    LOG_DEBUG("CongestionControl", "Entering Congestion Avoidance state");
}

void CongestionControl::EnterFastRecovery() {
    if (in_fast_recovery_) {
        return;  // 已经在快速恢复中
    }

    LOG_INFO("CongestionControl", "Entering Fast Recovery: cwnd=" + std::to_string(cwnd_) +
             " ssthresh=" + std::to_string(ssthresh_));

    // 快速恢复：
    // 1. ssthresh = cwnd / 2
    // 2. cwnd = ssthresh + 3 MSS
    // 3. 进入快速恢复状态

    ssthresh_ = std::max(2 * mss_, cwnd_ / 2);
    cwnd_ = ssthresh_ + 3 * mss_;
    state_ = STATE_FAST_RECOVERY;
    in_fast_recovery_ = true;
    ack_count_ = 0;
    duplicate_ack_count_ = 0;

    LOG_INFO("CongestionControl", "Fast Recovery entered: ssthresh=" + std::to_string(ssthresh_) +
             " cwnd=" + std::to_string(cwnd_));
}

void CongestionControl::ExitFastRecovery() {
    if (!in_fast_recovery_) {
        return;
    }

    in_fast_recovery_ = false;
    LOG_INFO("CongestionControl", "Exiting Fast Recovery, entering Congestion Avoidance");

    // 退出快速恢复，进入拥塞避免
    state_ = STATE_CONGESTION_AVOIDANCE;
    ack_count_ = 0;
    duplicate_ack_count_ = 0;

    LOG_INFO("CongestionControl", "Now in Congestion Avoidance: cwnd=" + std::to_string(cwnd_));
}
