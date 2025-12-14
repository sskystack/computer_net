#include "rdt_socket.h"
#include <cstdio>
#include <cstdarg>
#include <fstream>
#include <algorithm>

RdtSocket::RdtSocket()
    : sock(INVALID_SOCKET), connected(false), local_seq(0), remote_seq(0),
      recv_base(0), send_base(0), cong_state(SLOW_START), cwnd(1), ssthresh(10),
      dup_ack_count(0), last_ack_seq(0) {
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
        it = send_window.erase(it);
    }
}

bool RdtSocket::isPacketInWindow(uint32_t seq) {
    return seq >= recv_base && seq < recv_base + WINDOW_SIZE * DATA_SIZE;
}

void RdtSocket::processAck(uint32_t ack_seq) {
    if (ack_seq > last_ack_seq) {
        onNewAck();
        last_ack_seq = ack_seq;
        slideWindow(ack_seq);
    }
}

void RdtSocket::onNewAck() {
    if (cong_state == SLOW_START) {
        cwnd++;
        if (cwnd >= ssthresh) {
            cong_state = CONGESTION_AVOIDANCE;
        }
    } else if (cong_state == CONGESTION_AVOIDANCE) {
        cwnd++;
    }
    dup_ack_count = 0;
}

void RdtSocket::onDuplicateAck() {
    dup_ack_count++;
}

void RdtSocket::onTimeout() {
    ssthresh = (cwnd / 2 > 0) ? cwnd / 2 : 1;
    cwnd = 1;
    cong_state = SLOW_START;
}

void RdtSocket::retransmitPackets() {
    auto now = std::chrono::steady_clock::now();
    for (auto& entry : send_window) {
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
                sendAck(recv_base);
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

            sendAck(recv_base);

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
