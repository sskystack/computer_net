#ifndef UTILS_H
#define UTILS_H

#include <string>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <winsock2.h>
#include <cstdint>
#include <iostream>
#include <windows.h>
#include "protocol.h"

// 获取当前时间戳字符串
inline std::string getCurrentTime() {
    std::time_t now = std::time(nullptr);
    std::tm* timeinfo = std::localtime(&now);
    std::stringstream ss;
    ss << std::put_time(timeinfo, "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

// 发送字符串消息(带长度头)
inline bool sendMessage(SOCKET sock, const std::string& message) {
    try {
        uint32_t len = static_cast<uint32_t>(message.length());
        uint32_t network_len = htonl(len);

        // 发送长度头
        if (send(sock, reinterpret_cast<const char*>(&network_len),
                 sizeof(network_len), 0) != sizeof(network_len)) {
            return false;
        }

        // 发送消息内容(处理部分发送)
        int total_sent = 0;
        while (total_sent < static_cast<int>(len)) {
            int bytes_sent = send(sock, message.c_str() + total_sent,
                                 len - total_sent, 0);
            if (bytes_sent == SOCKET_ERROR || bytes_sent == 0) {
                return false;
            }
            total_sent += bytes_sent;
        }
        return true;
    } catch (...) {
        return false;
    }
}

// 接收字符串消息(带长度头)
inline bool recvMessage(SOCKET sock, std::string& message) {
    try {
        // 接收长度头
        uint32_t network_len = 0;
        int bytes_received = recv(sock, reinterpret_cast<char*>(&network_len),
                                 sizeof(network_len), 0);
        if (bytes_received != sizeof(network_len)) {
            return false;
        }

        // 转换为主机字节序并验证
        uint32_t len = ntohl(network_len);
        if (len == 0 || len > MAX_MESSAGE_SIZE) {
            return false;
        }

        // 接收消息内容(处理部分接收)
        message.resize(len);
        int total_received = 0;
        while (total_received < static_cast<int>(len)) {
            bytes_received = recv(sock, &message[total_received],
                                 len - total_received, 0);
            if (bytes_received == SOCKET_ERROR || bytes_received == 0) {
                return false;
            }
            total_received += bytes_received;
        }
        return true;
    } catch (...) {
        return false;
    }
}

// 初始化Winsock
inline bool initializeWinsock() {
    WSADATA wsaData;
    return WSAStartup(MAKEWORD(2, 2), &wsaData) == 0;
}

// 清理Winsock
inline void cleanupWinsock() {
    WSACleanup();
}

// 关闭Socket
inline void closeSocket(SOCKET& sock) {
    if (sock != INVALID_SOCKET) {
        closesocket(sock);
        sock = INVALID_SOCKET;
    }
}

// 设置控制台UTF-8编码支持
inline bool setConsoleUTF8() {
    SetConsoleCP(65001);       // 输入代码页
    SetConsoleOutputCP(65001); // 输出代码页
    return true;
}

// 打印带时间戳的消息
inline void printWithTimestamp(const std::string& message) {
    std::cout << "[" << getCurrentTime() << "] " << message << std::endl;
}

// 设置Socket接收超时
inline bool setRecvTimeout(SOCKET sock, DWORD timeout_ms) {
    return setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char*>(&timeout_ms),
                     sizeof(timeout_ms)) != SOCKET_ERROR;
}

// 检查是否为超时错误
inline bool isTimeoutError() {
    int error = WSAGetLastError();
    return (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK);
}

#endif // UTILS_H
