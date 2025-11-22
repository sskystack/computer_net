#include <iostream>
#include <winsock2.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <vector>
#include <string>
#include <memory>
#include "protocol.h"
#include "utils.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

// 客户端信息结构体
struct ClientInfo {
    SOCKET socket;
    string username;
    int user_id;
    bool active;

    explicit ClientInfo(SOCKET s = INVALID_SOCKET)
        : socket(s), user_id(-1), active(false) {}
};

// 全局变量
vector<shared_ptr<ClientInfo>> g_clients;
mutex g_clients_mutex;
int g_next_user_id = 1;
atomic<bool> g_server_running(true);
SOCKET g_listen_socket = INVALID_SOCKET;

// 函数声明
void handleClient(shared_ptr<ClientInfo> client_info);
void broadcastMessage(const Message& msg, int exclude_user_id = -1);
void removeClient(int user_id);
BOOL WINAPI consoleHandler(DWORD signal);
bool handleLogin(shared_ptr<ClientInfo> client_info);
void handleChatMessage(const Message& msg, shared_ptr<ClientInfo> client_info);
void handleUserListRequest(shared_ptr<ClientInfo> client_info);
void notifyUserJoined(const string& username, int user_id);
void notifyUserLeft(const string& username, int user_id);

// 广播消息给所有连接的客户端
void broadcastMessage(const Message& msg, int exclude_user_id) {
    string json_msg = JsonMessage::serialize(msg);
    lock_guard<mutex> lock(g_clients_mutex);

    for (auto& client : g_clients) {
        if (client && client->active && client->user_id != exclude_user_id) {
            if (!sendMessage(client->socket, json_msg)) {
                client->active = false;
            }
        }
    }
}

// 移除客户端
void removeClient(int user_id) {
    lock_guard<mutex> lock(g_clients_mutex);
    for (auto& client : g_clients) {
        if (client && client->user_id == user_id) {
            closeSocket(client->socket);
            client->active = false;
            break;
        }
    }
}

// 控制台信号处理(Ctrl+C)
BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT || signal == CTRL_CLOSE_EVENT) {
        cout << "\n\nShutting down server..." << endl;
        g_server_running.store(false);

        // 关闭监听socket,强制accept()立即返回
        if (g_listen_socket != INVALID_SOCKET) {
            closesocket(g_listen_socket);
            g_listen_socket = INVALID_SOCKET;
        }

        cout << "Server stopped." << endl;
        return TRUE;
    }
    return FALSE;
}

// 通知所有用户有新用户加入
void notifyUserJoined(const string& username, int user_id) {
    Message system_msg(MSG_SYSTEM, "System", username + " joined the chat");
    system_msg.timestamp = getCurrentTime();
    broadcastMessage(system_msg, user_id);
}

// 通知所有用户有用户离开
void notifyUserLeft(const string& username, int user_id) {
    Message logout_msg(MSG_SYSTEM, "System", username + " left the chat");
    logout_msg.timestamp = getCurrentTime();
    broadcastMessage(logout_msg, user_id);
}

// 处理登录流程
bool handleLogin(shared_ptr<ClientInfo> client_info) {
    string message;
    if (!recvMessage(client_info->socket, message)) {
        cout << "Failed to receive login message" << endl;
        return false;
    }

    Message login_msg = JsonMessage::deserialize(message);
    if (login_msg.type != MSG_LOGIN) {
        cout << "Expected LOGIN message, got type " << login_msg.type << endl;
        return false;
    }

    // 分配用户ID并更新客户端信息
    client_info->username = login_msg.username;
    client_info->user_id = g_next_user_id++;
    client_info->active = true;

    cout << "User logged in: " << client_info->username
         << " (ID: " << client_info->user_id << ")" << endl;

    // 发送登录确认消息
    Message ack_msg(MSG_LOGIN, "System", "Login successful");
    ack_msg.user_id = client_info->user_id;
    ack_msg.timestamp = getCurrentTime();
    if (!sendMessage(client_info->socket, JsonMessage::serialize(ack_msg))) {
        cout << "Failed to send login ACK" << endl;
        return false;
    }

    // 通知其他用户
    notifyUserJoined(client_info->username, client_info->user_id);
    return true;
}

// 处理聊天消息
void handleChatMessage(const Message& recv_msg, shared_ptr<ClientInfo> client_info) {
    Message chat_msg = recv_msg;
    chat_msg.username = client_info->username;
    chat_msg.user_id = client_info->user_id;
    chat_msg.timestamp = getCurrentTime();

    cout << "[" << chat_msg.timestamp << "] "
         << chat_msg.username << ": " << chat_msg.content << endl;

    broadcastMessage(chat_msg);
}

// 处理用户列表请求
void handleUserListRequest(shared_ptr<ClientInfo> client_info) {
    string user_list = "[";
    {
        lock_guard<mutex> lock(g_clients_mutex);
        bool first = true;
        for (const auto& client : g_clients) {
            if (client && client->active) {
                if (!first) user_list += ",";
                user_list += "\"" + client->username + "\"";
                first = false;
            }
        }
    }
    user_list += "]";

    Message list_msg(MSG_LIST, "System", user_list);
    list_msg.timestamp = getCurrentTime();
    sendMessage(client_info->socket, JsonMessage::serialize(list_msg));
}

// 处理单个客户端连接
void handleClient(shared_ptr<ClientInfo> client_info) {
    // 处理登录
    if (!handleLogin(client_info)) {
        closeSocket(client_info->socket);
        return;
    }

    // 设置socket接收超时,避免阻塞导致无法响应退出信号
    setRecvTimeout(client_info->socket, 1000);  // 1秒超时

    // 消息处理循环
    string message;
    while (client_info->active && g_server_running.load()) {
        if (!recvMessage(client_info->socket, message)) {
            // 检查是否是超时错误
            if (isTimeoutError()) {
                continue;  // 超时,继续等待
            }
            break;  // 其他错误或连接断开
        }

        Message recv_msg = JsonMessage::deserialize(message);

        switch (recv_msg.type) {
            case MSG_LOGOUT:
                cout << "User logged out: " << client_info->username << endl;
                notifyUserLeft(client_info->username, client_info->user_id);
                client_info->active = false;
                break;

            case MSG_CHAT:
                handleChatMessage(recv_msg, client_info);
                break;

            case MSG_LIST:
                handleUserListRequest(client_info);
                break;

            default:
                break;
        }
    }

    // 清理资源
    removeClient(client_info->user_id);
    cout << "Client disconnected: " << client_info->username << endl;
}

int main() {
    // 设置控制台UTF-8编码
    setConsoleUTF8();

    // 设置控制台信号处理
    if (!SetConsoleCtrlHandler(consoleHandler, TRUE)) {
        cerr << "Failed to set control handler" << endl;
        return 1;
    }

    // 初始化Winsock
    if (!initializeWinsock()) {
        cerr << "Failed to initialize Winsock" << endl;
        return 1;
    }

    // 创建服务器Socket
    g_listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_listen_socket == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        cleanupWinsock();
        return 1;
    }

    // 绑定Socket
    sockaddr_in server_addr = {};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    server_addr.sin_port = htons(DEFAULT_PORT);

    if (bind(g_listen_socket, reinterpret_cast<sockaddr*>(&server_addr),
             sizeof(server_addr)) == SOCKET_ERROR) {
        cerr << "Bind failed: " << WSAGetLastError() << endl;
        closeSocket(g_listen_socket);
        cleanupWinsock();
        return 1;
    }

    // 监听
    if (listen(g_listen_socket, SOMAXCONN) == SOCKET_ERROR) {
        cerr << "Listen failed: " << WSAGetLastError() << endl;
        closeSocket(g_listen_socket);
        cleanupWinsock();
        return 1;
    }

    cout << "Chat Server started on port " << DEFAULT_PORT << endl;
    cout << "Waiting for connections... (Press Ctrl+C to stop)" << endl;

    // 设置非阻塞accept超时,以便响应退出信号
    setRecvTimeout(g_listen_socket, 100);  // 100毫秒超时

    // 接受连接主循环
    while (g_server_running.load()) {
        sockaddr_in client_addr = {};
        int client_addr_len = sizeof(client_addr);

        SOCKET client_socket = accept(g_listen_socket,
                                      reinterpret_cast<sockaddr*>(&client_addr),
                                      &client_addr_len);

        if (client_socket == INVALID_SOCKET) {
            if (isTimeoutError()) {
                continue;  // 超时,继续循环检查g_server_running
            }
            if (!g_server_running.load()) {
                break;  // 正常退出
            }
            cerr << "Accept failed: " << WSAGetLastError() << endl;
            continue;
        }

        if (!g_server_running.load()) {
            closeSocket(client_socket);
            break;
        }

        cout << "New connection from " << inet_ntoa(client_addr.sin_addr)
             << ":" << ntohs(client_addr.sin_port) << endl;

        // 创建客户端信息并添加到列表
        auto client_info = make_shared<ClientInfo>(client_socket);
        {
            lock_guard<mutex> lock(g_clients_mutex);
            g_clients.push_back(client_info);
        }

        // 为客户端创建处理线程
        thread client_thread(handleClient, client_info);
        client_thread.detach();
    }

    cout << "Main loop exited." << endl;

    // 清理资源
    cout << "Cleaning up..." << endl;

    // 关闭监听socket(如果还没关闭)
    if (g_listen_socket != INVALID_SOCKET) {
        closesocket(g_listen_socket);
        g_listen_socket = INVALID_SOCKET;
    }

    // 标记所有客户端为非活动状态
    {
        lock_guard<mutex> lock(g_clients_mutex);
        for (auto& client : g_clients) {
            if (client) {
                client->active = false;
            }
        }
    }

    // 等待客户端线程自然结束
    cout << "Waiting for client threads to finish..." << endl;
    this_thread::sleep_for(chrono::seconds(1));

    // 关闭所有客户端连接
    {
        lock_guard<mutex> lock(g_clients_mutex);
        for (auto& client : g_clients) {
            if (client) {
                closeSocket(client->socket);
            }
        }
        g_clients.clear();
    }

    cleanupWinsock();
    cout << "Server shutdown complete." << endl;

    return 0;
}
