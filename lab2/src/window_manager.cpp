#include "window_manager.h"
#include "common.h"
#include <chrono>
#include <algorithm>

// ============================================
// 发送窗口实现
// ============================================

SendWindow::SendWindow(uint16_t window_size)
    : window_size_(window_size), base_seq_(0), next_seq_(0) {}

bool SendWindow::AddPacket(const Packet* packet, uint32_t seq) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查窗口是否已满
    if (packets_.size() >= window_size_) {
        LOG_WARN("SendWindow", "Window is full, cannot add packet");
        return false;
    }

    // 检查序列号是否重复
    if (packets_.find(seq) != packets_.end()) {
        LOG_WARN("SendWindow", "Duplicate sequence number: " + std::to_string(seq));
        return false;
    }

    SendWindowPacket swp;
    swp.packet = *packet;
    swp.seq = seq;
    swp.send_time = 0;  // 尚未发送
    swp.retransmit_count = 0;
    swp.acked = false;

    packets_[seq] = swp;

    // 更新 next_seq
    if (next_seq_ == base_seq_ || IsSeqAfter(seq, next_seq_)) {
        next_seq_ = seq + packet->header.len + (packet->header.flags & FLAG_DATA ? 1 : 0);
    }

    LOG_DEBUG("SendWindow", "Added packet: seq=" + std::to_string(seq) +
              " size=" + std::to_string(packets_.size()));
    return true;
}

bool SendWindow::CanSend(uint32_t seq) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = packets_.find(seq);
    if (it == packets_.end()) {
        return false;
    }
    return it->second.send_time == 0;  // 未发送过
}

bool SendWindow::AckPacket(uint32_t seq) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = packets_.find(seq);
    if (it == packets_.end()) {
        LOG_WARN("SendWindow", "ACK received for unknown sequence: " + std::to_string(seq));
        return false;
    }

    it->second.acked = true;
    LOG_DEBUG("SendWindow", "Packet ACKed: seq=" + std::to_string(seq));
    return true;
}

bool SendWindow::GetNextPacket(Packet* out_packet, uint32_t* out_seq) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& pair : packets_) {
        SendWindowPacket& swp = pair.second;
        if (swp.send_time == 0) {  // 未发送过
            *out_packet = swp.packet;
            *out_seq = swp.seq;
            swp.send_time = std::chrono::system_clock::now().time_since_epoch().count() / 1000000;
            LOG_DEBUG("SendWindow", "Getting next packet: seq=" + std::to_string(swp.seq));
            return true;
        }
    }

    return false;
}

bool SendWindow::GetRetransmitPacket(long long timeout_ms, Packet* out_packet, uint32_t* out_seq) {
    std::lock_guard<std::mutex> lock(mutex_);

    long long current_time = std::chrono::system_clock::now().time_since_epoch().count() / 1000000;

    for (auto& pair : packets_) {
        SendWindowPacket& swp = pair.second;
        if (!swp.acked && swp.send_time > 0 &&
            (current_time - swp.send_time) > timeout_ms) {
            *out_packet = swp.packet;
            *out_seq = swp.seq;
            swp.send_time = current_time;
            swp.retransmit_count++;
            LOG_INFO("SendWindow", "Retransmitting packet: seq=" + std::to_string(swp.seq) +
                     " retransmit_count=" + std::to_string(swp.retransmit_count));
            return true;
        }
    }

    return false;
}

uint32_t SendWindow::GetAckSeq() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (packets_.empty()) {
        return base_seq_;
    }

    // 找到最大的连续确认序列号
    uint32_t ack_seq = base_seq_;
    for (auto& pair : packets_) {
        if (pair.second.acked) {
            if (pair.first == ack_seq) {
                ack_seq = pair.first + 1;
            }
        }
    }

    return ack_seq;
}

void SendWindow::SlideWindow() {
    std::lock_guard<std::mutex> lock(mutex_);

    // 移除已确认的包
    auto it = packets_.begin();
    while (it != packets_.end()) {
        if (it->second.acked) {
            it = packets_.erase(it);
        } else {
            ++it;
        }
    }

    LOG_DEBUG("SendWindow", "Window slided, remaining packets: " + std::to_string(packets_.size()));
}

int SendWindow::GetUnackedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);

    int count = 0;
    for (const auto& pair : packets_) {
        if (!pair.second.acked) {
            count++;
        }
    }
    return count;
}

bool SendWindow::IsWindowFull() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return packets_.size() >= window_size_;
}

void SendWindow::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    packets_.clear();
    base_seq_ = 0;
    next_seq_ = 0;
    LOG_DEBUG("SendWindow", "Window cleared");
}

bool SendWindow::IsSeqInWindow(uint32_t seq) const {
    if (packets_.empty()) {
        return true;
    }
    uint32_t min_seq = packets_.begin()->first;
    uint32_t max_seq = packets_.rbegin()->first;
    return IsSeqAfterOrEqual(seq, min_seq) && IsSeqBeforeOrEqual(seq, max_seq);
}

// ============================================
// 接收窗口实现
// ============================================

ReceiveWindow::ReceiveWindow(uint16_t window_size)
    : window_size_(window_size), expected_seq_(0) {}

bool ReceiveWindow::AddPacket(const Packet* packet, uint32_t seq) {
    std::lock_guard<std::mutex> lock(mutex_);

    // 检查是否已接收过该序列号的包
    if (packets_.find(seq) != packets_.end()) {
        LOG_DEBUG("ReceiveWindow", "Duplicate packet: seq=" + std::to_string(seq));
        return false;  // 重复的包
    }

    // 检查序列号是否在窗口范围内
    if (!IsSeqInWindow(seq)) {
        LOG_WARN("ReceiveWindow", "Sequence out of window: seq=" + std::to_string(seq));
        return false;
    }

    ReceiveWindowPacket rwp;
    rwp.packet = *packet;
    rwp.seq = seq;
    rwp.received = true;

    packets_[seq] = rwp;

    LOG_DEBUG("ReceiveWindow", "Added packet: seq=" + std::to_string(seq) +
              " buffered=" + std::to_string(packets_.size()));
    return true;
}

bool ReceiveWindow::GetNextDeliverable(Packet* out_packet, uint32_t* out_seq) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = packets_.find(expected_seq_);
    if (it != packets_.end() && it->second.received) {
        *out_packet = it->second.packet;
        *out_seq = it->second.seq;

        packets_.erase(it);
        expected_seq_++;

        LOG_DEBUG("ReceiveWindow", "Delivering packet: seq=" + std::to_string(*out_seq));
        return true;
    }

    return false;
}

bool ReceiveWindow::IsPacketReceived(uint32_t seq) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = packets_.find(seq);
    return (it != packets_.end() && it->second.received);
}

uint32_t ReceiveWindow::GetExpectedSeq() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return expected_seq_;
}

int ReceiveWindow::GetBufferedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return packets_.size();
}

void ReceiveWindow::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    packets_.clear();
    expected_seq_ = 0;
    LOG_DEBUG("ReceiveWindow", "Window cleared");
}

bool ReceiveWindow::IsSeqInWindow(uint32_t seq) const {
    uint32_t window_end = expected_seq_ + window_size_;
    return IsSeqAfterOrEqual(seq, expected_seq_) && IsSeqBefore(seq, window_end);
}
