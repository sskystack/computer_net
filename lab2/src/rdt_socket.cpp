#include "rdt_socket.h"
#include "common.h"
#include <cstring>
#include <ctime>
#include <algorithm>

// ============ 构造和析构 ============

RDTSocket::RDTSocket()
    : udp_socket_(INVALID_SOCKET), is_server_(false), state_(STATE_CLOSED),
      local_seq_(0), remote_seq_(0), expected_seq_(0),
      send_window_(DEFAULT_WINDOW_SIZE),
      recv_window_(DEFAULT_WINDOW_SIZE),
      remote_window_size_(DEFAULT_WINDOW_SIZE),
      running_(false),
      rto_(RTO_INIT), rto_min_(RTO_MIN), rto_max_(RTO_MAX),
      last_ack_time_(0), recv_timeout_ms_(-1), start_time_(0) {

    stats_.bytes_sent = 0;
    stats_.bytes_received = 0;
    stats_.packets_sent = 0;
    stats_.packets_received = 0;
    stats_.packets_retransmitted = 0;
    stats_.packets_dropped = 0;
    stats_.average_throughput = 0.0;

    // 初始化本地和远端地址
    std::memset(&local_addr_, 0, sizeof(local_addr_));
    std::memset(&remote_addr_, 0, sizeof(remote_addr_));
    local_addr_.sin_family = AF_INET;
    remote_addr_.sin_family = AF_INET;
}

RDTSocket::~RDTSocket() {
    Close();
}

// ============ 初始化 ============

bool RDTSocket::Initialize(uint16_t window_size) {
    LOG_INFO("RDTSocket", "Initializing with window_size=" + std::to_string(window_size));

    // 初始化Winsock（Windows）
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        LOG_ERROR("RDTSocket", "WSAStartup failed");
        return false;
    }
#endif

    // 初始化校验和模块
    ChecksumCalculator::Initialize();

    // 创建UDP套接字
    udp_socket_ = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udp_socket_ == INVALID_SOCKET) {
        LOG_ERROR("RDTSocket", "Failed to create socket");
        return false;
    }

    // 设置SO_REUSEADDR选项
    int reuse = 1;
#ifdef _WIN32
    if (setsockopt(udp_socket_, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof(reuse)) < 0) {
        LOG_WARN("RDTSocket", "Failed to set SO_REUSEADDR");
    }
#else
    if (setsockopt(udp_socket_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0) {
        LOG_WARN("RDTSocket", "Failed to set SO_REUSEADDR");
    }
#endif

    // 设置套接字为非阻塞模式（接收线程中使用）
    // 在Windows中使用ioctlsocket，在Linux中使用fcntl

    // 初始化窗口
    send_window_.Clear();
    recv_window_.Clear();

    // 初始化拥塞控制
    congestion_control_.Initialize(DATA_SIZE);

    // 初始化随机序列号
    srand((unsigned int)time(NULL));
    local_seq_ = rand() % 10000;
    remote_seq_ = 0;
    expected_seq_ = 0;

    LOG_INFO("RDTSocket", "Initialization successful");
    return true;
}

// ============ 绑定和监听 ============

bool RDTSocket::Bind(uint16_t port) {
    LOG_DEBUG("RDTSocket", "Bind() called for port " + std::to_string(port));

    if (state_ != STATE_CLOSED) {
        LOG_WARN("RDTSocket", "Socket already bound");
        return false;
    }

    if (udp_socket_ == INVALID_SOCKET) {
        LOG_ERROR("RDTSocket", "Invalid socket");
        return false;
    }

    local_addr_.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr_.sin_port = htons(port);

    LOG_DEBUG("RDTSocket", "Calling bind() on socket");

    int result = bind(udp_socket_, (struct sockaddr*)&local_addr_, sizeof(local_addr_));
    if (result == SOCKET_ERROR) {
#ifdef _WIN32
        int err = WSAGetLastError();
        LOG_ERROR("RDTSocket", "Bind failed on port " + std::to_string(port) + " (error: " + std::to_string(err) + ")");
#else
        LOG_ERROR("RDTSocket", "Bind failed on port " + std::to_string(port));
#endif
        return false;
    }

    SetState(STATE_LISTEN);
    LOG_INFO("RDTSocket", "Bound to port " + std::to_string(port));
    return true;
}

bool RDTSocket::Listen(int backlog) {
    if (state_ != STATE_LISTEN) {
        LOG_WARN("RDTSocket", "Socket is not in LISTEN state");
        return false;
    }

    LOG_INFO("RDTSocket", "Listening for connections");
    return StartThreads();
}

bool RDTSocket::Accept(std::string& out_addr, uint16_t& out_port) {
    if (state_ != STATE_LISTEN) {
        LOG_WARN("RDTSocket", "Socket is not in LISTEN state");
        return false;
    }

    // 等待连接建立
    auto start = std::chrono::high_resolution_clock::now();
    while (state_ == STATE_LISTEN) {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed > 30000) {  // 30秒超时
            LOG_WARN("RDTSocket", "Accept timeout");
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (state_ != STATE_ESTABLISHED) {
        LOG_ERROR("RDTSocket", "Connection failed");
        return false;
    }

    // 获取远端地址和端口
    out_addr = inet_ntoa(remote_addr_.sin_addr);
    out_port = ntohs(remote_addr_.sin_port);

    LOG_INFO("RDTSocket", "Connection accepted from " + out_addr + ":" + std::to_string(out_port));
    return true;
}

// ============ 连接 ============

bool RDTSocket::Connect(const std::string& remote_ip, uint16_t remote_port) {
    if (state_ != STATE_CLOSED) {
        LOG_WARN("RDTSocket", "Socket is not in CLOSED state");
        return false;
    }

    // 绑定到一个随机端口
    local_addr_.sin_addr.s_addr = htonl(INADDR_ANY);
    local_addr_.sin_port = htons(0);  // 由系统分配

    if (bind(udp_socket_, (struct sockaddr*)&local_addr_, sizeof(local_addr_)) == SOCKET_ERROR) {
        LOG_ERROR("RDTSocket", "Bind failed");
        return false;
    }

    // 设置远端地址
    remote_addr_.sin_addr.s_addr = inet_addr(remote_ip.c_str());
    remote_addr_.sin_port = htons(remote_port);

    if (remote_addr_.sin_addr.s_addr == INADDR_NONE) {
        LOG_ERROR("RDTSocket", "Invalid remote IP: " + remote_ip);
        return false;
    }

    // 启动线程
    if (!StartThreads()) {
        LOG_ERROR("RDTSocket", "Failed to start threads");
        return false;
    }

    // 发送SYN包
    Packet syn_pkt;
    PacketHandler::CreateSynPacket(local_seq_, DEFAULT_WINDOW_SIZE, &syn_pkt);
    if (SendPacket(&syn_pkt) < 0) {
        LOG_ERROR("RDTSocket", "Failed to send SYN packet");
        StopThreads();
        return false;
    }

    // SYN消耗一个序列号
    local_seq_ += 1;

    SetState(STATE_SYN_SENT);
    LOG_INFO("RDTSocket", "Connecting to " + remote_ip + ":" + std::to_string(remote_port));

    // 等待连接建立
    auto start = std::chrono::high_resolution_clock::now();
    while (state_ == STATE_SYN_SENT) {
        auto now = std::chrono::high_resolution_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
        if (elapsed > 5000) {  // 5秒超时
            LOG_WARN("RDTSocket", "Connect timeout");
            SetState(STATE_CLOSED);
            StopThreads();
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (state_ != STATE_ESTABLISHED) {
        LOG_ERROR("RDTSocket", "Connection failed");
        return false;
    }

    start_time_ = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    LOG_INFO("RDTSocket", "Connected successfully");
    return true;
}

// ============ 关闭连接 ============

bool RDTSocket::Close() {
    if (state_ == STATE_CLOSED) {
        return true;
    }

    LOG_INFO("RDTSocket", "Closing connection");

    if (state_ == STATE_ESTABLISHED) {
        // 发送FIN包
        Packet fin_pkt;
        PacketHandler::CreateFinPacket(local_seq_, expected_seq_, DEFAULT_WINDOW_SIZE, &fin_pkt);
        SendPacket(&fin_pkt);
        SetState(STATE_FIN_WAIT_1);

        // 等待FIN-ACK或对端的FIN
        auto start = std::chrono::high_resolution_clock::now();
        while (state_ != STATE_TIME_WAIT && state_ != STATE_CLOSED) {
            auto now = std::chrono::high_resolution_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
            if (elapsed > 3000) {  // 3秒超时
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    StopThreads();
    Cleanup();

    LOG_INFO("RDTSocket", "Connection closed");
    return true;
}

// ============ 获取状态 ============

ConnectionState RDTSocket::GetState() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    return state_;
}

const char* RDTSocket::GetStateName() const {
    std::lock_guard<std::mutex> lock(state_mutex_);
    switch (state_) {
        case STATE_CLOSED: return "CLOSED";
        case STATE_LISTEN: return "LISTEN";
        case STATE_SYN_SENT: return "SYN_SENT";
        case STATE_SYN_RECV: return "SYN_RECV";
        case STATE_ESTABLISHED: return "ESTABLISHED";
        case STATE_FIN_WAIT_1: return "FIN_WAIT_1";
        case STATE_FIN_WAIT_2: return "FIN_WAIT_2";
        case STATE_CLOSING: return "CLOSING";
        case STATE_TIME_WAIT: return "TIME_WAIT";
        case STATE_CLOSE_WAIT: return "CLOSE_WAIT";
        case STATE_LAST_ACK: return "LAST_ACK";
        default: return "UNKNOWN";
    }
}

// ============ 发送和接收 ============

int RDTSocket::Send(const uint8_t* data, int length) {
    if (state_ != STATE_ESTABLISHED) {
        LOG_WARN("RDTSocket", "Connection not established");
        return -1;
    }

    if (length <= 0) {
        return 0;
    }

    int sent_total = 0;

    // 将数据分割成多个数据包发送
    while (sent_total < length) {
        int to_send = std::min(length - sent_total, DATA_SIZE);

        // 等待发送窗口有空间
        while (send_window_.IsWindowFull()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // 创建数据包
        Packet data_pkt;
        PacketHandler::CreateDataPacket(
            local_seq_,
            expected_seq_,
            recv_window_.GetWindowSize(),
            data + sent_total,
            to_send,
            &data_pkt
        );

        // 添加到发送窗口
        if (!send_window_.AddPacket(&data_pkt, local_seq_)) {
            LOG_WARN("RDTSocket", "Failed to add packet to send window");
            break;
        }

        // 发送数据包
        if (SendPacket(&data_pkt) < 0) {
            LOG_ERROR("RDTSocket", "Failed to send packet");
            break;
        }

        local_seq_ += to_send;
        sent_total += to_send;

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.packets_sent++;
            stats_.bytes_sent += to_send;
        }
    }

    return sent_total;
}

int RDTSocket::Recv(uint8_t* buffer, int length) {
    if (state_ != STATE_ESTABLISHED && state_ != STATE_CLOSE_WAIT) {
        LOG_WARN("RDTSocket", "Connection not in data transfer state");
        return -1;
    }

    // 从接收队列获取数据
    std::unique_lock<std::mutex> lock(recv_queue_mutex_);

    // 如果队列为空，等待数据到达
    if (recv_queue_.empty()) {
        std::chrono::milliseconds timeout(recv_timeout_ms_ > 0 ? recv_timeout_ms_ : 5000);
        if (!recv_cv_.wait_for(lock, timeout, [this] { return !recv_queue_.empty(); })) {
            LOG_DEBUG("RDTSocket", "Recv timeout");
            return 0;  // 超时返回0
        }
    }

    // 从队列中复制数据
    int copied = 0;
    while (copied < length && !recv_queue_.empty()) {
        buffer[copied++] = recv_queue_.front();
        recv_queue_.pop();
    }

    return copied;
}

// ============ 设置和获取 ============

RDTSocket::Statistics RDTSocket::GetStatistics() const {
    std::lock_guard<std::mutex> lock(stats_mutex_);
    Statistics result = stats_;

    // 计算吞吐率
    if (start_time_ > 0) {
        long long now = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        double elapsed_sec = (now - start_time_) / 1000000000.0;  // 转换为秒
        if (elapsed_sec > 0) {
            result.average_throughput = stats_.bytes_sent / elapsed_sec;
        }
    }

    return result;
}

void RDTSocket::SetRecvTimeout(int timeout_ms) {
    recv_timeout_ms_ = timeout_ms;
}

// ============ 私有方法 ============

void RDTSocket::SetState(ConnectionState new_state) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    // Get the state name without acquiring the lock again
    const char* old_name = "UNKNOWN";
    switch (state_) {
        case STATE_CLOSED: old_name = "CLOSED"; break;
        case STATE_LISTEN: old_name = "LISTEN"; break;
        case STATE_SYN_SENT: old_name = "SYN_SENT"; break;
        case STATE_SYN_RECV: old_name = "SYN_RECV"; break;
        case STATE_ESTABLISHED: old_name = "ESTABLISHED"; break;
        case STATE_FIN_WAIT_1: old_name = "FIN_WAIT_1"; break;
        case STATE_FIN_WAIT_2: old_name = "FIN_WAIT_2"; break;
        case STATE_CLOSING: old_name = "CLOSING"; break;
        case STATE_TIME_WAIT: old_name = "TIME_WAIT"; break;
        case STATE_CLOSE_WAIT: old_name = "CLOSE_WAIT"; break;
        case STATE_LAST_ACK: old_name = "LAST_ACK"; break;
    }

    const char* new_name = "UNKNOWN";
    switch (new_state) {
        case STATE_CLOSED: new_name = "CLOSED"; break;
        case STATE_LISTEN: new_name = "LISTEN"; break;
        case STATE_SYN_SENT: new_name = "SYN_SENT"; break;
        case STATE_SYN_RECV: new_name = "SYN_RECV"; break;
        case STATE_ESTABLISHED: new_name = "ESTABLISHED"; break;
        case STATE_FIN_WAIT_1: new_name = "FIN_WAIT_1"; break;
        case STATE_FIN_WAIT_2: new_name = "FIN_WAIT_2"; break;
        case STATE_CLOSING: new_name = "CLOSING"; break;
        case STATE_TIME_WAIT: new_name = "TIME_WAIT"; break;
        case STATE_CLOSE_WAIT: new_name = "CLOSE_WAIT"; break;
        case STATE_LAST_ACK: new_name = "LAST_ACK"; break;
    }

    LOG_DEBUG("RDTSocket", "State transition: " + std::string(old_name) + " -> " + std::string(new_name));
    state_ = new_state;
}

int RDTSocket::SendPacket(const Packet* packet) {
    uint8_t buffer[MAX_PACKET_SIZE];
    int size = PacketHandler::EncodePacket(packet, buffer, MAX_PACKET_SIZE);
    if (size < 0) {
        LOG_ERROR("RDTSocket", "Failed to encode packet");
        return -1;
    }

    int sent = sendto(udp_socket_, (const char*)buffer, size, 0,
                      (struct sockaddr*)&remote_addr_, sizeof(remote_addr_));
    if (sent == SOCKET_ERROR) {
        LOG_ERROR("RDTSocket", "sendto failed");
        return -1;
    }

    return sent;
}

bool RDTSocket::StartThreads() {
    if (running_) {
        return true;
    }

    {
        std::lock_guard<std::mutex> lock(running_mutex_);
        running_ = true;
    }

    recv_thread_ = std::thread(&RDTSocket::ReceiveThreadFunc, this);
    retransmit_thread_ = std::thread(&RDTSocket::RetransmitThreadFunc, this);

    return true;
}

void RDTSocket::StopThreads() {
    {
        std::lock_guard<std::mutex> lock(running_mutex_);
        running_ = false;
    }

    if (recv_thread_.joinable()) {
        recv_thread_.join();
    }

    if (retransmit_thread_.joinable()) {
        retransmit_thread_.join();
    }
}

void RDTSocket::Cleanup() {
    CloseSocket();
    send_window_.Clear();
    recv_window_.Clear();
    SetState(STATE_CLOSED);
}

void RDTSocket::CloseSocket() {
    if (udp_socket_ != INVALID_SOCKET) {
#ifdef _WIN32
        closesocket(udp_socket_);
        WSACleanup();
#else
        close(udp_socket_);
#endif
        udp_socket_ = INVALID_SOCKET;
    }
}

void RDTSocket::UpdateRTO(long long sample_rtt) {
    // 简化的RTO计算（标准TCP算法）
    rto_ = std::max(rto_min_, std::min(rto_max_, (uint32_t)(sample_rtt * 2)));
    LOG_DEBUG("RDTSocket", "RTO updated to " + std::to_string(rto_) + "ms");
}

// ============ 接收线程 ============

void RDTSocket::ReceiveThreadFunc() {
    LOG_INFO("RDTSocket", "Receive thread started");

    uint8_t buffer[MAX_PACKET_SIZE];
    struct sockaddr_in src_addr;
    socklen_t src_addr_len = sizeof(src_addr);

    while (running_) {
        // 设置接收超时
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 100000;  // 100ms
#ifdef _WIN32
        setsockopt(udp_socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof(tv));
#else
        setsockopt(udp_socket_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

        int recv_size = recvfrom(udp_socket_, (char*)buffer, MAX_PACKET_SIZE, 0,
                                 (struct sockaddr*)&src_addr, &src_addr_len);

        if (recv_size == SOCKET_ERROR) {
            // 超时或错误，继续
            continue;
        }

        if (recv_size < (int)sizeof(ProtocolHeader)) {
            LOG_WARN("RDTSocket", "Received packet too small");
            continue;
        }

        // 解码数据包
        Packet pkt;
        if (PacketHandler::DecodePacket(buffer, recv_size, &pkt) < 0) {
            LOG_WARN("RDTSocket", "Failed to decode packet");
            continue;
        }

        // 验证校验和
        if (!PacketHandler::VerifyChecksum(&pkt)) {
            LOG_WARN("RDTSocket", "Checksum verification failed");
            {
                std::lock_guard<std::mutex> lock(stats_mutex_);
                stats_.packets_dropped++;
            }
            continue;
        }

        // 处理数据包
        HandleReceivedPacket(pkt, src_addr);

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.packets_received++;
        }
    }

    LOG_INFO("RDTSocket", "Receive thread stopped");
}

// ============ 重传线程 ============

void RDTSocket::RetransmitThreadFunc() {
    LOG_INFO("RDTSocket", "Retransmit thread started");

    while (running_) {
        if (state_ == STATE_ESTABLISHED || state_ == STATE_CLOSE_WAIT) {
            RetransmitWaitingPackets();
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(TIMEOUT_CHECK));
    }

    LOG_INFO("RDTSocket", "Retransmit thread stopped");
}

void RDTSocket::RetransmitWaitingPackets() {
    Packet pkt;
    uint32_t seq;

    while (send_window_.GetRetransmitPacket(rto_, &pkt, &seq)) {
        LOG_INFO("RDTSocket", "Retransmitting packet: seq=" + std::to_string(seq));
        SendPacket(&pkt);

        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.packets_retransmitted++;
        }

        // 触发拥塞控制的超时处理
        congestion_control_.OnTimeout();
    }
}

// ============ 数据包处理 ============

void RDTSocket::HandleReceivedPacket(const Packet& packet, const struct sockaddr_in& remote_addr) {
    // 更新远端地址（以防来自新地址）
    if (state_ == STATE_SYN_SENT || state_ == STATE_LISTEN) {
        remote_addr_ = remote_addr;
    }

    // 根据标志位分发处理
    if (packet.header.flags & FLAG_SYN) {
        if (packet.header.flags & FLAG_ACK) {
            HandleSynAck(packet);
        } else {
            HandleSyn(packet);
        }
    } else if (packet.header.flags & FLAG_ACK) {
        HandleAck(packet);
    }

    if (packet.header.flags & FLAG_FIN) {
        HandleFin(packet);
    } else if (packet.header.flags & FLAG_DATA) {
        HandleData(packet);
    }
}

void RDTSocket::HandleSyn(const Packet& packet) {
    if (state_ != STATE_LISTEN) {
        LOG_WARN("RDTSocket", "SYN received in wrong state: " + std::to_string(state_));
        return;
    }

    LOG_INFO("RDTSocket", "SYN received: seq=" + std::to_string(packet.header.seq));

    remote_seq_ = packet.header.seq;
    expected_seq_ = remote_seq_ + 1;
    remote_window_size_ = packet.header.wnd;

    // 发送SYN-ACK
    Packet syn_ack_pkt;
    PacketHandler::CreateSynAckPacket(local_seq_, expected_seq_, DEFAULT_WINDOW_SIZE, &syn_ack_pkt);
    SendPacket(&syn_ack_pkt);

    // SYN消耗一个序列号
    local_seq_ += 1;

    SetState(STATE_SYN_RECV);
    LOG_INFO("RDTSocket", "SYN-ACK sent");
}

void RDTSocket::HandleSynAck(const Packet& packet) {
    if (state_ != STATE_SYN_SENT) {
        LOG_WARN("RDTSocket", "SYN-ACK received in wrong state: " + std::to_string(state_));
        return;
    }

    LOG_INFO("RDTSocket", "SYN-ACK received: seq=" + std::to_string(packet.header.seq) +
             " ack=" + std::to_string(packet.header.ack));

    remote_seq_ = packet.header.seq;
    expected_seq_ = remote_seq_ + 1;
    remote_window_size_ = packet.header.wnd;

    // 重要：SYN消耗一个序列号，所以DATA应该从local_seq_+1开始
    // 但这里我们维持local_seq_不变，在Send()中首次创建数据包时才使用
    // 发送SYN-ACK的ACK不消耗序列号

    // 发送ACK完成三路握手
    Packet ack_pkt;
    PacketHandler::CreateAckPacket(local_seq_, expected_seq_, DEFAULT_WINDOW_SIZE, &ack_pkt);
    SendPacket(&ack_pkt);

    SetState(STATE_ESTABLISHED);
    start_time_ = std::chrono::high_resolution_clock::now().time_since_epoch().count();
    LOG_INFO("RDTSocket", "Connected! Connection established");
}

void RDTSocket::HandleAck(const Packet& packet) {
    if (state_ == STATE_SYN_RECV) {
        // ACK for SYN-ACK
        LOG_INFO("RDTSocket", "ACK received for SYN-ACK");
        SetState(STATE_ESTABLISHED);
        start_time_ = std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return;
    }

    if (state_ != STATE_ESTABLISHED && state_ != STATE_FIN_WAIT_1 &&
        state_ != STATE_FIN_WAIT_2 && state_ != STATE_CLOSE_WAIT) {
        return;
    }

    LOG_DEBUG("RDTSocket", "ACK received: ack=" + std::to_string(packet.header.ack));

    // 更新远端窗口大小
    remote_window_size_ = packet.header.wnd;

    // 确认数据包
    send_window_.AckPacket(packet.header.ack);
    send_window_.SlideWindow();

    // 触发拥塞控制
    congestion_control_.OnAck(packet.header.ack);

    // 更新RTO
    last_ack_time_ = std::chrono::high_resolution_clock::now().time_since_epoch().count() / 1000000;
}

void RDTSocket::HandleData(const Packet& packet) {
    if (state_ != STATE_ESTABLISHED && state_ != STATE_FIN_WAIT_1 && state_ != STATE_FIN_WAIT_2) {
        LOG_WARN("RDTSocket", "Data received in wrong state");
        return;
    }

    LOG_DEBUG("RDTSocket", "Data received: seq=" + std::to_string(packet.header.seq) +
              " len=" + std::to_string(packet.header.len));

    // 添加到接收窗口
    if (!recv_window_.AddPacket(&packet, packet.header.seq)) {
        LOG_DEBUG("RDTSocket", "Packet already received or out of window");
        // 仍然需要发送ACK确认
    }

    // 从接收窗口提取有序的数据
    Packet out_pkt;
    uint32_t out_seq;
    while (recv_window_.GetNextDeliverable(&out_pkt, &out_seq)) {
        // 将数据添加到接收队列
        for (int i = 0; i < out_pkt.header.len; i++) {
            recv_queue_.push(out_pkt.data[i]);
        }
        expected_seq_ = out_seq + out_pkt.header.len;
    }

    // 发送ACK
    Packet ack_pkt;
    PacketHandler::CreateAckPacket(local_seq_, expected_seq_, recv_window_.GetWindowSize(), &ack_pkt);
    SendPacket(&ack_pkt);

    // 通知接收线程有数据到达
    recv_cv_.notify_one();

    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats_.bytes_received += packet.header.len;
    }
}

void RDTSocket::HandleFin(const Packet& packet) {
    LOG_INFO("RDTSocket", "FIN received: seq=" + std::to_string(packet.header.seq));

    if (state_ == STATE_ESTABLISHED) {
        // 被动关闭：ESTABLISHED -> CLOSE_WAIT
        SetState(STATE_CLOSE_WAIT);
        expected_seq_ = packet.header.seq + 1;

        // 发送ACK
        Packet ack_pkt;
        PacketHandler::CreateAckPacket(local_seq_, expected_seq_, recv_window_.GetWindowSize(), &ack_pkt);
        SendPacket(&ack_pkt);

        // 通知应用层连接关闭
        recv_cv_.notify_one();
    } else if (state_ == STATE_FIN_WAIT_1) {
        // 同时关闭：FIN_WAIT_1 -> CLOSING
        SetState(STATE_CLOSING);
        expected_seq_ = packet.header.seq + 1;

        // 发送ACK
        Packet ack_pkt;
        PacketHandler::CreateAckPacket(local_seq_, expected_seq_, recv_window_.GetWindowSize(), &ack_pkt);
        SendPacket(&ack_pkt);
    } else if (state_ == STATE_FIN_WAIT_2) {
        // 对端延迟关闭：FIN_WAIT_2 -> TIME_WAIT
        SetState(STATE_TIME_WAIT);
        expected_seq_ = packet.header.seq + 1;

        // 发送ACK
        Packet ack_pkt;
        PacketHandler::CreateAckPacket(local_seq_, expected_seq_, recv_window_.GetWindowSize(), &ack_pkt);
        SendPacket(&ack_pkt);
    }
}

