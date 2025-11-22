#include <iostream>
#include <thread>
#include <string>
#include <cstring>
#include <atomic>
#include <chrono>
#include <cstdlib>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    #define socklen_t int
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define SOCKET int
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    #define closesocket close
#endif

#include "protocol.h"

// 全局变量
std::atomic<bool> client_running(true);
SOCKET client_socket = INVALID_SOCKET;
std::string username;

// 函数声明
void receive_messages();
void send_message(MessageType type, const std::string& content = "");
void display_help();

/**
 * 接收服务器消息的线程函数
 */
void receive_messages() {
    char buffer[MAX_PACKET_LEN];
    ChatMessage msg;

    while (client_running) {
        int received = recv(client_socket, buffer, MAX_PACKET_LEN, 0);

        if (received <= 0) {
            if (client_running) {
                std::cout << "\n[系统] 与服务器的连接已断开" << std::endl;
                std::cout << "[系统] 程序即将退出..." << std::endl;
                client_running = false;

                // 给主线程一点时间显示消息
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                exit(0); // 强制退出程序
            }
            break;
        }

        // 反序列化消息
        if (deserialize_message(buffer, received, msg) < 0) {
            std::cerr << "[错误] 消息格式错误" << std::endl;
            continue;
        }

        // 处理不同类型的消息
        switch (msg.type) {
            case MSG_LOGIN: {
                std::string message_content(msg.message, msg.message_len);
                std::cout << "[系统] " << message_content << std::endl;
                break;
            }

            case MSG_LOGOUT: {
                std::string message_content(msg.message, msg.message_len);
                std::cout << "[系统] " << message_content << std::endl;
                break;
            }

            case MSG_CHAT: {
                std::string sender(msg.username, msg.username_len);
                std::string message_content(msg.message, msg.message_len);
                std::string time_str = format_timestamp(msg.timestamp);

                // 如果是自己发送的消息，显示"我"，否则显示发送者用户名
                std::string display_name = (sender == username) ? "我" : sender;
                std::cout << "[" << time_str << "] [" << display_name << "] " << message_content << std::endl;
                break;
            }

            case MSG_SERVER_SHUTDOWN: {
                std::string message_content(msg.message, msg.message_len);
                std::cout << "[系统] " << message_content << std::endl;
                client_running = false;

                // 主动关闭socket，让主线程能够退出
                std::cout << "[系统] 程序即将退出..." << std::endl;
#ifdef _WIN32
                // Windows下关闭socket输入，让主线程的getline能够返回
                closesocket(client_socket);
#else
                close(client_socket);
#endif
                // 给主线程一点时间处理
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                exit(0); // 强制退出程序
                break;
            }
        }
    }
}

/**
 * 发送消息到服务器
 */
void send_message(MessageType type, const std::string& content) {
    ChatMessage msg;
    msg.type = type;
    msg.username_len = username.length();
    strncpy(msg.username, username.c_str(), MAX_USERNAME_LEN - 1);
    msg.message_len = content.length();
    strncpy(msg.message, content.c_str(), MAX_MESSAGE_LEN - 1);
    msg.timestamp = get_current_timestamp(); // 添加当前时间戳

    char buffer[MAX_PACKET_LEN];
    int len = serialize_message(msg, buffer, MAX_PACKET_LEN);

    if (len > 0) {
        int sent = send(client_socket, buffer, len, 0);
        if (sent == SOCKET_ERROR) {
            std::cerr << "[错误] 发送消息失败" << std::endl;
        }
    }
}

/**
 * 显示帮助信息
 */
void display_help() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "    聊天客户端帮助" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "命令说明:" << std::endl;
    std::cout << "  /quit       - 退出聊天程序" << std::endl;
    std::cout << "  /help       - 显示帮助信息" << std::endl;
    std::cout << "  其他输入    - 发送聊天消息" << std::endl;
    std::cout << "\n注意:" << std::endl;
    std::cout << "  - 要发送包含'quit'的聊天内容，直接输入即可" << std::endl;
    std::cout << "  - 只有输入 '/quit' 才会退出程序" << std::endl;
    std::cout << "  - 支持中英文聊天内容" << std::endl;
    std::cout << "  - 支持显示消息发送时间" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

int main(int argc, char* argv[]) {
    // Windows平台初始化Winsock
#ifdef _WIN32
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        std::cerr << "[错误] WSAStartup 失败" << std::endl;
        return 1;
    }
#endif

    // 设置控制台UTF-8编码（Windows）
#ifdef _WIN32
    system("chcp 65001 > nul");
#endif

    std::cout << "========================================" << std::endl;
    std::cout << "    多人聊天客户端" << std::endl;
    std::cout << "========================================" << std::endl;

    // 输入用户名
    std::cout << "请输入用户名: ";
    std::getline(std::cin, username);

    if (username.empty() || username.length() > MAX_USERNAME_LEN - 1) {
        std::cerr << "[错误] 用户名无效（长度应在1-31个字符之间）" << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // 创建套接字
    client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET) {
        std::cerr << "[错误] 创建套接字失败" << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // 连接服务器
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(8888);

    // 默认连接本地服务器，可以修改为其他IP
    std::string server_ip = "127.0.0.1";
    if (argc > 1) {
        server_ip = argv[1];
    }

    server_addr.sin_addr.s_addr = inet_addr(server_ip.c_str());
    if (server_addr.sin_addr.s_addr == INADDR_NONE) {
        std::cerr << "[错误] 无效的服务器IP地址: " << server_ip << std::endl;
        closesocket(client_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "[信息] 正在连接服务器 " << server_ip << ":8888..." << std::endl;

    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[错误] 连接服务器失败" << std::endl;
        closesocket(client_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "[信息] 已连接到服务器" << std::endl;

    // 发送登录消息
    send_message(MSG_LOGIN, username + " 加入了聊天");

    // 启动接收消息线程
    std::thread receive_thread(receive_messages);

    // 显示帮助信息
    display_help();

    // 主线程处理用户输入
    std::string input;
    std::cout << "开始聊天（输入 /help 查看帮助）:" << std::endl;
    std::cout << "当前用户：" << username << std::endl;

    while (client_running) {
        if (!std::getline(std::cin, input)) {
            // 输入流结束（Ctrl+C或其他情况）
            break;
        }

        if (!client_running) {
            break;
        }

        // 处理特殊命令
        if (input == "/quit") {
            std::cout << "[系统] 正在退出聊天..." << std::endl;
            send_message(MSG_LOGOUT, "");
            client_running = false;
            break;
        } else if (input == "/help") {
            display_help();
            continue;
        } else if (input.empty()) {
            continue;
        }

        // 检查消息长度
        if (input.length() > MAX_MESSAGE_LEN - 1) {
            std::cout << "[警告] 消息太长，请输入较短的消息（最大" << (MAX_MESSAGE_LEN - 1) << "个字符）" << std::endl;
            continue;
        }

        // 发送聊天消息
        send_message(MSG_CHAT, input);
    }

    // 关闭连接
    closesocket(client_socket);

    // 等待接收线程结束
    if (receive_thread.joinable()) {
        receive_thread.join();
    }

    std::cout << "[系统] 客户端已退出" << std::endl;

    // Windows平台清理Winsock
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}