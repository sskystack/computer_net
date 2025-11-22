#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <cstring>
#include <ctime>
#include <sstream>
#include <iomanip>

/**
 * 聊天协议说明文档
 * ==================
 *
 * 1. 协议概述
 *    本协议基于TCP流式套接字，采用自定义应用层协议
 *    支持多客户端同时连接，服务器转发消息
 *
 * 2. 消息格式
 *    所有消息由固定头部和可变消息体组成
 *    [消息类型(1字节)] [用户名长度(1字节)] [用户名(变长)] [消息长度(2字节)] [消息内容(变长)]
 *
 * 3. 消息类型定义
 *    MSG_LOGIN   (0x01): 客户端登录消息
 *    MSG_LOGOUT  (0x02): 客户端退出消息
 *    MSG_CHAT    (0x03): 普通聊天消息
 *    MSG_SERVER_SHUTDOWN (0x04): 服务器关闭消息
 *    MSG_USER_LIST (0x05): 在线用户列表
 *
 * 4. 消息流程
 *    a) 客户端连接 -> 发送 MSG_LOGIN
 *    b) 服务器广播新用户加入
 *    c) 客户端发送聊天内容 -> MSG_CHAT
 *    d) 服务器转发给所有其他客户端
 *    e) 客户端断开 -> 发送 MSG_LOGOUT 或检测连接断开
 *    f) 服务器关闭 -> 发送 MSG_SERVER_SHUTDOWN 给所有客户端
 *
 * 5. 编码
 *    消息内容支持UTF-8编码，兼容中英文
 *
 * 6. 可靠性
 *    基于TCP协议，保证消息顺序和可靠传输
 *    通过返回值检测是否有数据包丢失或连接断开
 */

// 消息类型定义
enum MessageType {
    MSG_LOGIN = 0x01,           // 登录消息
    MSG_LOGOUT = 0x02,          // 退出消息
    MSG_CHAT = 0x03,            // 聊天消息
    MSG_SERVER_SHUTDOWN = 0x04, // 服务器关闭
    MSG_USER_LIST = 0x05        // 用户列表（可选扩展）
};

// 最大用户名长度
#define MAX_USERNAME_LEN 32
// 最大消息长度
#define MAX_MESSAGE_LEN 1024
// 最大完整包长度 (增加了8字节的时间戳)
#define MAX_PACKET_LEN (1 + 1 + MAX_USERNAME_LEN + 2 + MAX_MESSAGE_LEN + 8)

// 消息包结构
struct ChatMessage {
    unsigned char type;                  // 消息类型
    unsigned char username_len;          // 用户名长度
    char username[MAX_USERNAME_LEN];     // 用户名
    unsigned short message_len;          // 消息长度
    char message[MAX_MESSAGE_LEN];       // 消息内容
    unsigned long long timestamp;        // 时间戳 (Unix时间戳，8字节)

    // 构造函数
    ChatMessage() : type(0), username_len(0), message_len(0), timestamp(0) {
        memset(username, 0, MAX_USERNAME_LEN);
        memset(message, 0, MAX_MESSAGE_LEN);
    }
};

/**
 * 序列化消息到缓冲区
 * @param msg 要序列化的消息
 * @param buffer 输出缓冲区
 * @param buffer_size 缓冲区大小
 * @return 序列化后的字节数，失败返回-1
 */
inline int serialize_message(const ChatMessage& msg, char* buffer, int buffer_size) {
    int offset = 0;

    // 检查缓冲区大小 (增加8字节时间戳)
    int required_size = 1 + 1 + msg.username_len + 2 + msg.message_len + 8;
    if (buffer_size < required_size) {
        return -1;
    }

    // 消息类型
    buffer[offset++] = msg.type;

    // 用户名长度和用户名
    buffer[offset++] = msg.username_len;
    memcpy(buffer + offset, msg.username, msg.username_len);
    offset += msg.username_len;

    // 消息长度（网络字节序）
    buffer[offset++] = (msg.message_len >> 8) & 0xFF;
    buffer[offset++] = msg.message_len & 0xFF;

    // 消息内容
    memcpy(buffer + offset, msg.message, msg.message_len);
    offset += msg.message_len;

    // 时间戳（8字节，网络字节序）
    for (int i = 7; i >= 0; i--) {
        buffer[offset++] = (msg.timestamp >> (i * 8)) & 0xFF;
    }

    return offset;
}

/**
 * 从缓冲区反序列化消息
 * @param buffer 输入缓冲区
 * @param buffer_size 缓冲区大小
 * @param msg 输出消息
 * @return 消耗的字节数，失败返回-1
 */
inline int deserialize_message(const char* buffer, int buffer_size, ChatMessage& msg) {
    if (buffer_size < 12) { // 至少需要类型+用户名长度+消息长度+时间戳 (4+8=12)
        return -1;
    }

    int offset = 0;

    // 消息类型
    msg.type = buffer[offset++];

    // 用户名长度
    msg.username_len = buffer[offset++];
    if (msg.username_len > MAX_USERNAME_LEN || offset + msg.username_len + 2 + 8 > buffer_size) {
        return -1;
    }

    // 用户名
    memcpy(msg.username, buffer + offset, msg.username_len);
    msg.username[msg.username_len] = '\0';
    offset += msg.username_len;

    // 消息长度（网络字节序）
    msg.message_len = ((unsigned char)buffer[offset] << 8) | (unsigned char)buffer[offset + 1];
    offset += 2;

    if (msg.message_len > MAX_MESSAGE_LEN || offset + msg.message_len + 8 > buffer_size) {
        return -1;
    }

    // 消息内容
    memcpy(msg.message, buffer + offset, msg.message_len);
    msg.message[msg.message_len] = '\0';
    offset += msg.message_len;

    // 时间戳（8字节，网络字节序）
    msg.timestamp = 0;
    for (int i = 0; i < 8; i++) {
        msg.timestamp = (msg.timestamp << 8) | (unsigned char)buffer[offset++];
    }

    return offset;
}

/**
 * 获取当前时间戳
 * @return Unix时间戳
 */
inline unsigned long long get_current_timestamp() {
    return static_cast<unsigned long long>(time(nullptr));
}

/**
 * 将时间戳格式化为可读字符串
 * @param timestamp Unix时间戳
 * @return 格式化的时间字符串 (HH:MM:SS)
 */
inline std::string format_timestamp(unsigned long long timestamp) {
    time_t time_val = static_cast<time_t>(timestamp);
    struct tm* time_info = localtime(&time_val);

    std::ostringstream oss;
    oss << std::setfill('0')
        << std::setw(2) << time_info->tm_hour << ":"
        << std::setw(2) << time_info->tm_min << ":"
        << std::setw(2) << time_info->tm_sec;

    return oss.str();
}

#endif // PROTOCOL_H
