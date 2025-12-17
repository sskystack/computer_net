#include "rdt_socket.h"
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <algorithm>

RdtSocket::RdtSocket()
    : sock(INVALID_SOCKET), connected(false), local_seq(0), remote_seq(0),
      recv_base(0), send_base(0), cong_state(SLOW_START), cwnd(1), ssthresh(10),
      dup_ack_count(0), last_ack_seq(0), ca_acc(0) {
    memset(&local_addr, 0, sizeof(local_addr));
    memset(&remote_addr, 0, sizeof(remote_addr));
}

RdtSocket::~RdtSocket() {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
    }
}

void RdtSocket::log(const char* format, ...) {
    va_list args;
    va_start(args, format);
    char buffer[2048];
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    printf("%s\n", buffer);
    fflush(stdout);
}

void RdtSocket::logPacketHeader(const char* label, const PacketHeader& header) {
    log("[%s] Header details: seq=%u, ack=%u, type=%u, data_len=%u, file_size=%u, checksum=0x%04x",
        label, header.seq_num, header.ack_num, header.packet_type,
        header.data_length, header.file_size, header.checksum);

    // 打印头部前32字节的十六进制
    log("[%s] Header hex (first 32 bytes):", label);
    const uint8_t* bytes = (const uint8_t*)&header;
    char hex_buffer[256] = {0};
    for (int i = 0; i < 32; i++) {
        sprintf(hex_buffer + i*3, "%02x ", bytes[i]);
    }
    log("[%s]   %s", label, hex_buffer);
}

bool RdtSocket::bind(const char* ip, uint16_t port) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        log("[ERROR] Socket created failed");
        return false;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = inet_addr(ip);

    if (::bind(sock, (sockaddr*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
        log("[ERROR] Bind failed: %s:%d", ip, port);
        closesocket(sock);
        sock = INVALID_SOCKET;
        return false;
    }

    log("[BIND] Local address bound: %s:%d", ip, port);
    return true;
}

bool RdtSocket::connect(const char* ip, uint16_t port) {
    log("[CONN] Starting connection to %s:%d", ip, port);

    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(port);
    remote_addr.sin_addr.s_addr = inet_addr(ip);

    Packet syn_pkt;
    syn_pkt.header.packet_type = PKT_SYN;
    syn_pkt.header.seq_num = local_seq;
    syn_pkt.header.data_length = 0;
    syn_pkt.header.checksum = 0;  // 计算前清零
    syn_pkt.header.checksum = calculateChecksum(&syn_pkt.header,
                                               sizeof(syn_pkt.header) - sizeof(syn_pkt.header.checksum));

    log("[CONN] Sending SYN (seq=%u)", local_seq);
    if (!sendPacket(syn_pkt)) {
        log("[ERROR] Failed to send SYN");
        return false;
    }

    auto start = std::chrono::steady_clock::now();
    Packet ack_pkt;
    while (true) {
        if (recvPacket(ack_pkt, 100)) {
            if (ack_pkt.header.packet_type == PKT_SYN_ACK) {
                remote_seq = ack_pkt.header.seq_num;
                recv_base = remote_seq;
                log("[CONN] Received SYN-ACK (seq=%u, ack=%u)", remote_seq, ack_pkt.header.ack_num);

                Packet final_ack;
                final_ack.header.packet_type = PKT_ACK;
                final_ack.header.seq_num = local_seq;
                final_ack.header.ack_num = remote_seq;
                final_ack.header.checksum = 0;  // 计算前清零
                final_ack.header.checksum = calculateChecksum(&final_ack.header,
                                                             sizeof(final_ack.header) - sizeof(final_ack.header.checksum));

                log("[CONN] Sending ACK (seq=%u, ack=%u)", final_ack.header.seq_num, final_ack.header.ack_num);
                if (sendPacket(final_ack)) {
                    connected = true;
                    log("[CONN] Connection established!");
                    return true;
                }
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > CONNECT_TIMEOUT_MS) {
            log("[ERROR] Connection timeout");
            return false;
        }
    }
}

bool RdtSocket::listen(uint16_t port) {
    sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) {
        log("[ERROR] Socket creation failed");
        return false;
    }

    local_addr.sin_family = AF_INET;
    local_addr.sin_port = htons(port);
    local_addr.sin_addr.s_addr = INADDR_ANY;

    if (::bind(sock, (sockaddr*)&local_addr, sizeof(local_addr)) == SOCKET_ERROR) {
        log("[ERROR] Bind port failed: %d", port);
        closesocket(sock);
        sock = INVALID_SOCKET;
        return false;
    }

    log("[LISTEN] Listening on port: %d", port);
    return true;
}

RdtSocket* RdtSocket::accept() {
    Packet syn_pkt;
    int addr_len = sizeof(remote_addr);

    log("[ACCEPT] Waiting for connection...");

    if (recvfrom(sock, (char*)&syn_pkt, sizeof(syn_pkt), 0,
                 (sockaddr*)&remote_addr, &addr_len) == SOCKET_ERROR) {
        log("[ERROR] Failed to receive SYN");
        return nullptr;
    }

    if (syn_pkt.header.packet_type != PKT_SYN) {
        log("[ERROR] Expected SYN, got type: %d", syn_pkt.header.packet_type);
        return nullptr;
    }

    log("[ACCEPT] Received connection from %s:%d (seq=%u)",
        inet_ntoa(remote_addr.sin_addr), ntohs(remote_addr.sin_port), syn_pkt.header.seq_num);

    RdtSocket* new_sock = new RdtSocket();
    new_sock->sock = this->sock;
    new_sock->remote_addr = this->remote_addr;
    new_sock->local_addr = this->local_addr;
    new_sock->remote_seq = syn_pkt.header.seq_num;
    new_sock->recv_base = syn_pkt.header.seq_num;
    new_sock->local_seq = 100;

    Packet syn_ack;
    syn_ack.header.packet_type = PKT_SYN_ACK;
    syn_ack.header.seq_num = new_sock->local_seq;
    syn_ack.header.ack_num = new_sock->remote_seq;
    syn_ack.header.checksum = 0;  // 计算前清零
    syn_ack.header.checksum = calculateChecksum(&syn_ack.header,
                                               sizeof(syn_ack.header) - sizeof(syn_ack.header.checksum));

    log("[ACCEPT] Sending SYN-ACK (seq=%u, ack=%u)", syn_ack.header.seq_num, syn_ack.header.ack_num);
    if (!new_sock->sendPacket(syn_ack)) {
        log("[ERROR] Failed to send SYN-ACK");
        delete new_sock;
        return nullptr;
    }

    Packet ack_pkt;
    if (!new_sock->recvPacket(ack_pkt, CONNECT_TIMEOUT_MS)) {
        log("[ERROR] ACK timeout");
        delete new_sock;
        return nullptr;
    }

    if (ack_pkt.header.packet_type == PKT_ACK) {
        new_sock->connected = true;
        log("[ACCEPT] Connection established!");
        return new_sock;
    }

    delete new_sock;
    return nullptr;
}

bool RdtSocket::sendPacket(const Packet& pkt) {
    if (sendto(sock, (const char*)&pkt, sizeof(Packet), 0,
               (sockaddr*)&remote_addr, sizeof(remote_addr)) == SOCKET_ERROR) {
        return false;
    }
    return true;
}

bool RdtSocket::recvPacket(Packet& pkt, uint32_t timeout_ms) {
    int timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));

    int addr_len = sizeof(remote_addr);
    int n = recvfrom(sock, (char*)&pkt, sizeof(Packet), 0,
                     (sockaddr*)&remote_addr, &addr_len);

    return (n != SOCKET_ERROR);
}

uint32_t RdtSocket::getEffectiveWindow() {
    return std::min((uint32_t)WINDOW_SIZE, cwnd);
}

bool RdtSocket::canSendPacket() {
    return (uint32_t)send_window.size() < getEffectiveWindow();
}

void RdtSocket::slideWindow(uint32_t ack_seq) {
    auto it = send_window.begin();
    while (it != send_window.end() && it->first < ack_seq) {
        // 移除SACK记录（已连续确认的包）
        sacked_packets.erase(it->first);
        it = send_window.erase(it);
    }
}

bool RdtSocket::isPacketInWindow(uint32_t seq) {
    return seq >= recv_base && seq < recv_base + WINDOW_SIZE * DATA_SIZE;
}

void RdtSocket::processAck(uint32_t ack_seq) {
    if (ack_seq > last_ack_seq) {
        // 新的 ACK，重置重复计数
        onNewAck();
        last_ack_seq = ack_seq;
        slideWindow(ack_seq);
    } else if (ack_seq == last_ack_seq) {
        // 重复 ACK
        onDuplicateAck();
        log("[DUPACK] Duplicate ACK received (ack=%u), count=%u", ack_seq, dup_ack_count);
        
        if (dup_ack_count == 3) {
            // 3 个重复 ACK，触发快速重传和快速恢复
            log("[DUPACK] 3 duplicate ACKs received! Triggering Fast Retransmit and Fast Recovery");
            
            // 设置阈值为当前拥塞窗口的一半
            ssthresh = (cwnd / 2 > 0) ? cwnd / 2 : 1;
            log("[DUPACK] ssthresh set to %u", ssthresh);
            
            // 快速恢复（Reno 的经典写法）
            // cwnd = ssthresh + 3*MSS（因为收到了 3 个 dupACK）
            cwnd = ssthresh + 3;  // 这里 3 代表 3*MSS，假设一个包就是一个 MSS
            ca_acc = 0;  // 重置累加器
            cong_state = CONGESTION_AVOIDANCE;
            log("[DUPACK] cwnd set to %u (ssthresh + 3), ca_acc reset, entering Congestion Avoidance", cwnd);
            
            // 快速重传：重传丢失的数据包
            // 找到需要重传的最早未确认包
            if (!send_window.empty()) {
                auto first_unacked = send_window.begin();
                log("[DUPACK] Fast Retransmit: retransmitting packet (seq=%u)", first_unacked->first);
                sendPacket(first_unacked->second.packet);
                first_unacked->second.send_time = std::chrono::steady_clock::now();
                first_unacked->second.retransmit_count++;
            }
        }
    }
    // 如果 ack_seq < last_ack_seq，说明是更早的 ACK，直接忽略
}

void RdtSocket::onNewAck() {
    // 重置重复 ACK 计数
    dup_ack_count = 0;
    
    if (cong_state == SLOW_START) {
        cwnd++;
        if (cwnd >= ssthresh) {
            cong_state = CONGESTION_AVOIDANCE;
            log("[NEWACK] Entering Congestion Avoidance, cwnd=%u, ssthresh=%u", cwnd, ssthresh);
        }
    } else if (cong_state == CONGESTION_AVOIDANCE) {
        // 拥塞避免：每个 RTT 增加 1 MSS（MSS=1）
        // 在一个 RTT 内约有 cwnd 个 ACK，所以每个 ACK 增加 1/cwnd
        // 使用累加器避免浮点数：攒够 cwnd 次 ACK 再 +1
        ca_acc += 1;
        if (ca_acc >= cwnd) {
            cwnd += 1;
            ca_acc = 0;
            log("[NEWACK] Congestion Avoidance: cwnd increased to %u (accumulated %u ACKs)", cwnd, cwnd - 1);
        }
    }
}

void RdtSocket::onDuplicateAck() {
    dup_ack_count++;
    log("[ONDUPACK] dup_ack_count incremented to %u", dup_ack_count);
}

void RdtSocket::onTimeout() {
    ssthresh = (cwnd / 2 > 0) ? cwnd / 2 : 1;
    cwnd = 1;
    cong_state = SLOW_START;
    ca_acc = 0;  // 重置累加器，重新开始慢启动
    log("[TIMEOUT] Timeout: cwnd reset to 1, ssthresh to %u, entering Slow Start", ssthresh);
}

void RdtSocket::retransmitPackets() {
    auto now = std::chrono::steady_clock::now();
    for (auto& entry : send_window) {
        // 如果已通过SACK块确认，则不需重传
        if (sacked_packets.find(entry.first) != sacked_packets.end()) {
            log("[SACK] Packet (seq=%u) is SACKED, skipping retransmit", entry.first);
            continue;
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - entry.second.send_time).count();
        if (elapsed > TIMEOUT_MS) {
            log("[RETX] Packet timeout, retransmitting (seq=%u)", entry.first);
            entry.second.send_time = now;
            entry.second.retransmit_count++;
            sendPacket(entry.second.packet);
            onTimeout();
        }
    }
}

bool RdtSocket::isTimerExpired(uint32_t seq) {
    if (send_window.find(seq) == send_window.end()) return false;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - send_window[seq].send_time).count();
    return elapsed > TIMEOUT_MS;
}

bool RdtSocket::sendAck(uint32_t ack_seq) {
    Packet ack;
    ack.header.packet_type = PKT_ACK;
    ack.header.seq_num = local_seq;
    ack.header.ack_num = ack_seq;
    ack.header.data_length = 0;
    ack.header.checksum = 0;  // 计算前清零
    ack.header.checksum = calculateChecksum(&ack.header,
                                           sizeof(ack.header) - sizeof(ack.header.checksum));
    return sendPacket(ack);
}

void RdtSocket::generateSackBlocks(SackBlock* blocks, uint8_t& count) {
    count = 0;
    if (recv_buffer.empty()) return;

    // 遍历recv_buffer，找出不连续的已缓存数据块
    uint32_t prev_end = recv_base;
    SackBlock current_block;
    bool in_block = false;

    for (auto& entry : recv_buffer) {
        uint32_t seq = entry.first;

        // 如果当前包在recv_base之前或重叠，跳过
        if (seq < recv_base) continue;

        // 检查是否有间隙
        if (seq > prev_end) {
            // 间隙前有缓存数据，保存当前块
            if (in_block && count < MAX_SACK_BLOCKS) {
                blocks[count++] = current_block;
                in_block = false;
            }
        }

        // 开始或扩展当前块
        if (!in_block) {
            current_block.start = seq;
            in_block = true;
        }

        // 更新块的结束位置
        const Packet& pkt = entry.second;
        current_block.end = seq + pkt.header.data_length;
        prev_end = current_block.end;
    }

    // 保存最后一个块
    if (in_block && count < MAX_SACK_BLOCKS) {
        blocks[count++] = current_block;
    }

    if (count > 0) {
        log("[SACK] Generated %u SACK blocks:", count);
        for (uint8_t i = 0; i < count; i++) {
            log("[SACK]   Block[%u]: %u-%u", i, blocks[i].start, blocks[i].end);
        }
    }
}

bool RdtSocket::sendAckWithSack(uint32_t ack_seq) {
    Packet ack;
    ack.header.packet_type = PKT_ACK;
    ack.header.seq_num = local_seq;
    ack.header.ack_num = ack_seq;

    // 生成SACK块
    SackBlock sack_blocks[MAX_SACK_BLOCKS];
    uint8_t sack_count = 0;
    generateSackBlocks(sack_blocks, sack_count);

    // 编码SACK块到data部分
    uint16_t data_len = 0;
    if (sack_count > 0) {
        data_len = encodeSackBlocks(sack_blocks, sack_count, ack.data, DATA_SIZE);
    }

    ack.header.data_length = data_len;
    ack.header.checksum = 0;  // 计算前清零
    ack.header.checksum = calculateChecksum(&ack.header,
                                           sizeof(ack.header) - sizeof(ack.header.checksum));
    if (data_len > 0) {
        ack.header.checksum += calculateChecksum(ack.data, data_len);
    }

    return sendPacket(ack);
}

bool RdtSocket::sendFile(const char* filename) {
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
        log("[ERROR] Cannot open file: %s", filename);
        return false;
    }

    file.seekg(0, std::ios::end);
    uint32_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    const char* base_filename = strrchr(filename, '\\');
    if (!base_filename) base_filename = strrchr(filename, '/');
    if (!base_filename) base_filename = filename;
    else base_filename++;

    log("\n========== File Transfer Started ==========");
    log("[SEND] Filename: %s", base_filename);
    log("[SEND] File path: %s", filename);
    log("[SEND] File size: %u bytes", file_size);
    log("==========================================\n");

    uint32_t sent = 0;
    uint32_t seq = local_seq;
    bool first_data = true;

    while (sent < file_size) {
        if (!canSendPacket()) {
            Packet ack_pkt;
            if (recvPacket(ack_pkt, 50)) {
                if (ack_pkt.header.packet_type == PKT_ACK) {
                    processAck(ack_pkt.header.ack_num);

                    // 解析SACK块
                    if (ack_pkt.header.data_length > 0) {
                        SackBlock sack_blocks[MAX_SACK_BLOCKS];
                        uint8_t sack_count = decodeSackBlocks(ack_pkt.data, ack_pkt.header.data_length,
                                                             sack_blocks, MAX_SACK_BLOCKS);
                        if (sack_count > 0) {
                            for (uint8_t i = 0; i < sack_count; i++) {
                                // 标记这个范围内的包为已缓存确认
                                for (uint32_t seq_num = sack_blocks[i].start; seq_num < sack_blocks[i].end; seq_num++) {
                                    if (send_window.find(seq_num) != send_window.end()) {
                                        sacked_packets.insert(seq_num);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            retransmitPackets();
            continue;
        }

        Packet data_pkt;
        uint16_t to_send = std::min((uint32_t)DATA_SIZE, file_size - sent);

        file.read(data_pkt.data, to_send);
        data_pkt.header.packet_type = PKT_DATA;
        data_pkt.header.seq_num = seq;
        data_pkt.header.ack_num = recv_base;
        data_pkt.header.data_length = to_send;
        data_pkt.header.file_size = file_size;
        data_pkt.header.checksum = 0;  // 计算前清零
        // 确保reserved字段被初始化为0
        memset(data_pkt.header.reserved, 0, sizeof(data_pkt.header.reserved));

        if (first_data) {
            strncpy_s(data_pkt.header.filename, sizeof(data_pkt.header.filename),
                     base_filename, _TRUNCATE);
            first_data = false;
        }

        // 计算校验和：header（不包括checksum字段）+ data部分
        data_pkt.header.checksum = 0;  // 计算前清零
        uint32_t header_checksum = calculateChecksum(&data_pkt.header, sizeof(data_pkt.header) - sizeof(data_pkt.header.checksum));
        uint32_t data_checksum = calculateChecksum(data_pkt.data, to_send);
        data_pkt.header.checksum = (header_checksum + data_checksum) & 0xFFFF;

        SendWindowEntry entry;
        entry.packet = data_pkt;
        entry.send_time = std::chrono::steady_clock::now();
        entry.retransmit_count = 0;
        send_window[seq] = entry;

        log("[SEND] Data (seq=%u, len=%u, win=%zu, cwnd=%u)",
            seq, to_send, send_window.size(), cwnd);
        sendPacket(data_pkt);

        sent += to_send;
        seq += to_send;

        Packet ack_pkt;
        if (recvPacket(ack_pkt, 10)) {
            if (ack_pkt.header.packet_type == PKT_ACK) {
                processAck(ack_pkt.header.ack_num);

                // 解析SACK块
                if (ack_pkt.header.data_length > 0) {
                    SackBlock sack_blocks[MAX_SACK_BLOCKS];
                    uint8_t sack_count = decodeSackBlocks(ack_pkt.data, ack_pkt.header.data_length,
                                                         sack_blocks, MAX_SACK_BLOCKS);
                    if (sack_count > 0) {
                        log("[SACK] Received %u SACK blocks:", sack_count);
                        for (uint8_t i = 0; i < sack_count; i++) {
                            log("[SACK]   Block[%u]: %u-%u", i, sack_blocks[i].start, sack_blocks[i].end);
                            // 标记这个范围内的包为已缓存确认
                            for (uint32_t seq = sack_blocks[i].start; seq < sack_blocks[i].end; seq++) {
                                if (send_window.find(seq) != send_window.end()) {
                                    sacked_packets.insert(seq);
                                }
                            }
                        }
                    }
                }
            }
        }
        retransmitPackets();
    }

    log("[SEND] Waiting for final ACKs...");
    auto start = std::chrono::steady_clock::now();
    while (!send_window.empty()) {
        Packet ack_pkt;
        if (recvPacket(ack_pkt, 100)) {
            if (ack_pkt.header.packet_type == PKT_ACK) {
                processAck(ack_pkt.header.ack_num);

                // 解析SACK块
                if (ack_pkt.header.data_length > 0) {
                    SackBlock sack_blocks[MAX_SACK_BLOCKS];
                    uint8_t sack_count = decodeSackBlocks(ack_pkt.data, ack_pkt.header.data_length,
                                                         sack_blocks, MAX_SACK_BLOCKS);
                    if (sack_count > 0) {
                        log("[SACK] Received %u SACK blocks:", sack_count);
                        for (uint8_t i = 0; i < sack_count; i++) {
                            log("[SACK]   Block[%u]: %u-%u", i, sack_blocks[i].start, sack_blocks[i].end);
                            // 标记这个范围内的包为已缓存确认
                            for (uint32_t seq = sack_blocks[i].start; seq < sack_blocks[i].end; seq++) {
                                if (send_window.find(seq) != send_window.end()) {
                                    sacked_packets.insert(seq);
                                }
                            }
                        }
                    }
                }
            }
        }
        retransmitPackets();

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start).count();
        if (elapsed > CONNECT_TIMEOUT_MS) break;
    }

    file.close();
    log("[SEND] File transfer completed");

    Packet fin;
    fin.header.packet_type = PKT_FIN;
    fin.header.seq_num = seq;
    fin.header.ack_num = recv_base;
    fin.header.checksum = 0;  // 计算前清零
    fin.header.checksum = calculateChecksum(&fin.header,
                                           sizeof(fin.header) - sizeof(fin.header.checksum));

    log("[SEND] Sending FIN");
    sendPacket(fin);

    Packet fin_ack;
    if (recvPacket(fin_ack, CONNECT_TIMEOUT_MS) && fin_ack.header.packet_type == PKT_FIN_ACK) {
        log("[SEND] Connection closed");
    }

    connected = false;
    return true;
}

bool RdtSocket::recvFile(const char* save_path) {
    std::ofstream file(save_path, std::ios::binary);
    if (!file) {
        log("[ERROR] Cannot create file: %s", save_path);
        return false;
    }

    log("\n========== File Reception Started ==========");
    log("[RECV] Save path: %s", save_path);

    uint32_t total_size = 0;
    uint32_t received = 0;
    char filename_received[32] = {0};
    bool first_packet = true;

    while (true) {
        Packet data_pkt;
        if (!recvPacket(data_pkt, CONNECT_TIMEOUT_MS)) {
            log("[ERROR] Receive timeout");
            file.close();
            return false;
        }

        if (data_pkt.header.packet_type == PKT_DATA) {
            // 校验和验证：header（不包括checksum字段）+ data部分
            uint32_t received_checksum = data_pkt.header.checksum;  // 保存接收到的checksum
            data_pkt.header.checksum = 0;  // 清零后再计算
            uint32_t header_checksum = calculateChecksum(&data_pkt.header, sizeof(data_pkt.header) - sizeof(data_pkt.header.checksum));
            uint32_t data_checksum = calculateChecksum(data_pkt.data, data_pkt.header.data_length);
            uint32_t expected = (header_checksum + data_checksum) & 0xFFFF;

            if (expected != received_checksum) {
                log("[ERROR] Checksum error (seq=%u, expected=0x%04x, got=0x%04x)",
                    data_pkt.header.seq_num, expected, received_checksum);
                continue;
            }

            if (!isPacketInWindow(data_pkt.header.seq_num)) {
                log("[RECV] Packet out of window (seq=%u)", data_pkt.header.seq_num);
                sendAckWithSack(recv_base);
                continue;
            }

            if (first_packet) {
                total_size = data_pkt.header.file_size;
                strncpy_s(filename_received, sizeof(filename_received),
                         data_pkt.header.filename, _TRUNCATE);
                log("[RECV] Filename: %s", filename_received);
                log("[RECV] File size: %u bytes", total_size);
                first_packet = false;
            }

            recv_buffer[data_pkt.header.seq_num] = data_pkt;

            while (recv_buffer.find(recv_base) != recv_buffer.end()) {
                Packet& pkt = recv_buffer[recv_base];
                file.write(pkt.data, pkt.header.data_length);
                received += pkt.header.data_length;
                log("[RECV] Progress: %u / %u bytes", received, total_size);

                recv_buffer.erase(recv_base);
                recv_base += pkt.header.data_length;
            }

            sendAckWithSack(recv_base);

            if (received >= total_size) {
                log("[RECV] All data received");
                break;
            }

        } else if (data_pkt.header.packet_type == PKT_FIN) {
            log("[RECV] Received FIN");

            Packet fin_ack;
            fin_ack.header.packet_type = PKT_FIN_ACK;
            fin_ack.header.seq_num = local_seq;
            fin_ack.header.ack_num = data_pkt.header.seq_num;
            fin_ack.header.checksum = 0;  // 计算前清零
            fin_ack.header.checksum = calculateChecksum(&fin_ack.header,
                                                       sizeof(fin_ack.header) - sizeof(fin_ack.header.checksum));
            sendPacket(fin_ack);

            connected = false;
            break;
        }
    }

    file.close();
    log("[RECV] File received successfully");
    log("[RECV] Received: %u bytes", received);
    log("[RECV] Connection closed");
    log("==========================================\n");

    return true;
}

bool RdtSocket::close() {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
    connected = false;
    return true;
}

int RdtSocket::sendData(const void* data, size_t length) {
    Packet pkt;
    pkt.header.packet_type = PKT_DATA;
    pkt.header.seq_num = local_seq;
    pkt.header.ack_num = recv_base;
    pkt.header.data_length = std::min(length, (size_t)DATA_SIZE);
    memcpy(pkt.data, data, pkt.header.data_length);
    pkt.header.checksum = 0;  // 计算前清零
    pkt.header.checksum = calculateChecksum(&pkt.header, sizeof(pkt.header) - 4) +
                          calculateChecksum(pkt.data, pkt.header.data_length);

    return sendPacket(pkt) ? pkt.header.data_length : -1;
}

int RdtSocket::recvData(void* buffer, size_t max_length) {
    Packet pkt;
    if (recvPacket(pkt, TIMEOUT_MS)) {
        if (pkt.header.packet_type == PKT_DATA) {
            size_t to_copy = std::min(max_length, (size_t)pkt.header.data_length);
            memcpy(buffer, pkt.data, to_copy);
            return to_copy;
        }
    }
    return -1;
}
