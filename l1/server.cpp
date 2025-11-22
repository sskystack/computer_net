#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <algorithm>
#include <cstring>
#include <string>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")
    typedef int socklen_t;
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

// 客户端信息结构
struct ClientInfo {
    SOCKET socket;
    std::string username;
    bool active;

    ClientInfo(SOCKET s) : socket(s), active(true) {}
};

// 全局变量
std::vector<ClientInfo*> clients;
std::mutex clients_mutex;
bool server_running = true;

// 函数声明
void broadcast_message(const ChatMessage& msg, SOCKET exclude_socket = INVALID_SOCKET);
void handle_client(ClientInfo* client);
void accept_clients(SOCKET listen_socket);
void server_command_handler();
void display_online_users();

/**
 * 广播消息给所有客户端（可排除某个客户端）
 */
void broadcast_message(const ChatMessage& msg, SOCKET exclude_socket) {
    // 如果服务器正在关闭，不发送新消息
    if (!server_running) {
        return;
    }

    char buffer[MAX_PACKET_LEN];
    int len = serialize_message(msg, buffer, MAX_PACKET_LEN);

    if (len <= 0) {
        std::cerr << "[错误] 序列化消息失败" << std::endl;
        return;
    }

    std::lock_guard<std::mutex> lock(clients_mutex);
    for (auto client : clients) {
        if (client->active && client->socket != exclude_socket && client->socket != INVALID_SOCKET) {
            int sent = send(client->socket, buffer, len, 0);
            if (sent == SOCKET_ERROR) {
                // 发送失败时标记客户端为非活跃状态，但不输出错误
                client->active = false;
            }
        }
    }
}

/**
 * 显示当前在线用户信息
 */
void display_online_users() {
    std::lock_guard<std::mutex> lock(clients_mutex);

    // 统计活跃用户
    std::vector<std::string> online_users;
    for (auto client : clients) {
        if (client->active && !client->username.empty()) {
            online_users.push_back(client->username);
        }
    }

    std::cout << "[统计] 当前在线人数: " << online_users.size() << " 人" << std::endl;

    if (!online_users.empty()) {
        std::cout << "[统计] 在线用户列表: ";
        for (size_t i = 0; i < online_users.size(); ++i) {
            std::cout << online_users[i];
            if (i < online_users.size() - 1) {
                std::cout << ", ";
            }
        }
        std::cout << std::endl;
    } else {
        std::cout << "[统计] 当前没有在线用户" << std::endl;
    }
}

/**
 * 处理单个客户端的连接
 */
void handle_client(ClientInfo* client) {
    char buffer[MAX_PACKET_LEN];
    ChatMessage msg;
    bool logged_in = false;
    std::string safe_username; // 保存用户名的安全副本

    while (server_running && client->active) {
        // 接收消息类型和用户名长度
        int received = recv(client->socket, buffer, MAX_PACKET_LEN, 0);

        if (received <= 0) {
            // 连接断开
            if (logged_in && !safe_username.empty()) {
                std::cout << "[信息] 用户 " << safe_username << " 连接断开" << std::endl;

                // 只有在服务器仍在运行时才广播用户离开消息
                if (server_running) {
                    ChatMessage logout_msg;
                    logout_msg.type = MSG_LOGOUT;
                    logout_msg.username_len = safe_username.length();
                    strncpy(logout_msg.username, safe_username.c_str(), MAX_USERNAME_LEN - 1);
                    logout_msg.username[MAX_USERNAME_LEN - 1] = '\0'; // 确保字符串终止
                    std::string leave_text = safe_username + " 退出了聊天";
                    logout_msg.message_len = leave_text.length();
                    strncpy(logout_msg.message, leave_text.c_str(), MAX_MESSAGE_LEN - 1);
                    logout_msg.message[MAX_MESSAGE_LEN - 1] = '\0'; // 确保字符串终止

                    broadcast_message(logout_msg, client->socket);

                    // 显示当前在线用户统计
                    display_online_users();
                }
            }
            break;
        }

        // 反序列化消息
        if (deserialize_message(buffer, received, msg) < 0) {
            std::cerr << "[错误] 反序列化消息失败" << std::endl;
            continue;
        }

        // 处理不同类型的消息
        switch (msg.type) {
            case MSG_LOGIN: {
                client->username = std::string(msg.username, msg.username_len);
                safe_username = client->username; // 保存安全副本
                logged_in = true;
                std::cout << "[信息] 用户 " << safe_username << " 加入聊天" << std::endl;

                // 广播用户加入消息
                ChatMessage join_msg;
                join_msg.type = MSG_LOGIN;
                join_msg.username_len = msg.username_len;
                strncpy(join_msg.username, msg.username, MAX_USERNAME_LEN - 1);
                join_msg.username[MAX_USERNAME_LEN - 1] = '\0'; // 确保字符串终止
                std::string join_text = safe_username + " 加入了聊天";
                join_msg.message_len = join_text.length();
                strncpy(join_msg.message, join_text.c_str(), MAX_MESSAGE_LEN - 1);
                join_msg.message[MAX_MESSAGE_LEN - 1] = '\0'; // 确保字符串终止

                broadcast_message(join_msg, client->socket);

                // 显示当前在线用户统计
                display_online_users();
                break;
            }

            case MSG_LOGOUT: {
                std::cout << "[信息] 用户 " << safe_username << " 主动退出" << std::endl;

                // 广播用户离开消息
                std::string leave_text = safe_username + " 退出了聊天";
                msg.message_len = leave_text.length();
                strncpy(msg.message, leave_text.c_str(), MAX_MESSAGE_LEN - 1);
                msg.message[MAX_MESSAGE_LEN - 1] = '\0'; // 确保字符串终止

                broadcast_message(msg, client->socket);
                client->active = false;

                // 显示当前在线用户统计
                display_online_users();
                break;
            }

            case MSG_CHAT: {
                std::string message_content(msg.message, msg.message_len);
                std::string time_str = format_timestamp(msg.timestamp);
                std::cout << "[" << time_str << "] [" << safe_username << "] " << message_content << std::endl;

                // 转发聊天消息给其他客户端
                broadcast_message(msg, client->socket);
                break;
            }
        }
    }

    // 清理客户端资源
    closesocket(client->socket);
    client->active = false;
}

/**
 * 接受新客户端连接
 */
void accept_clients(SOCKET listen_socket) {
    while (server_running) {
        struct sockaddr_in client_addr;
        socklen_t addr_len = sizeof(client_addr);

        SOCKET client_socket = accept(listen_socket, (struct sockaddr*)&client_addr, &addr_len);

        if (client_socket == INVALID_SOCKET) {
            if (server_running) {
                std::cerr << "[错误] 接受客户端连接失败" << std::endl;
            }
            continue;
        }

        std::string client_ip = inet_ntoa(client_addr.sin_addr);
        std::cout << "[信息] 新客户端连接: " << client_ip << ":" << ntohs(client_addr.sin_port) << std::endl;

        // 创建客户端信息并启动处理线程
        ClientInfo* client = new ClientInfo(client_socket);

        {
            std::lock_guard<std::mutex> lock(clients_mutex);
            clients.push_back(client);
        }

        std::thread client_thread(handle_client, client);
        client_thread.detach();
    }
}

/**
 * 服务器命令处理（主线程）
 */
void server_command_handler() {
    std::string command;
    std::cout << "服务器命令: 输入 'quit' 关闭服务器" << std::endl;

    while (server_running) {
        std::getline(std::cin, command);

        if (command == "quit") {
            std::cout << "[信息] 正在关闭服务器..." << std::endl;
            server_running = false;

            // 向所有客户端发送服务器关闭消息
            ChatMessage shutdown_msg;
            shutdown_msg.type = MSG_SERVER_SHUTDOWN;
            shutdown_msg.username_len = 6;
            strncpy(shutdown_msg.username, "Server", MAX_USERNAME_LEN - 1);
            std::string shutdown_text = "服务器已断开连接";
            shutdown_msg.message_len = shutdown_text.length();
            strncpy(shutdown_msg.message, shutdown_text.c_str(), MAX_MESSAGE_LEN - 1);

            broadcast_message(shutdown_msg);
            break;
        }
    }
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

    // 创建监听套接字
    SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_socket == INVALID_SOCKET) {
        std::cerr << "[错误] 创建套接字失败" << std::endl;
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // 设置地址重用
    int reuse = 1;
    setsockopt(listen_socket, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    // 绑定地址和端口
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8888);

    if (bind(listen_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) == SOCKET_ERROR) {
        std::cerr << "[错误] 绑定地址失败" << std::endl;
        closesocket(listen_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    // 开始监听
    if (listen(listen_socket, 5) == SOCKET_ERROR) {
        std::cerr << "[错误] 监听失败" << std::endl;
        closesocket(listen_socket);
#ifdef _WIN32
        WSACleanup();
#endif
        return 1;
    }

    std::cout << "========================================" << std::endl;
    std::cout << "    多人聊天服务器" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "[信息] 服务器已启动，监听端口 8888" << std::endl;

    // 启动接受客户端线程
    std::thread accept_thread(accept_clients, listen_socket);

    // 主线程处理服务器命令
    server_command_handler();

    // 关闭监听套接字
    closesocket(listen_socket);

    // 等待接受线程结束
    if (accept_thread.joinable()) {
        accept_thread.join();
    }

    // 关闭所有客户端连接
    {
        std::lock_guard<std::mutex> lock(clients_mutex);
        for (auto client : clients) {
            if (client->active) {
                closesocket(client->socket);
            }
            delete client;
        }
        clients.clear();
    }

    std::cout << "[信息] 服务器已关闭" << std::endl;

    // Windows平台清理Winsock
#ifdef _WIN32
    WSACleanup();
#endif

    return 0;
}
