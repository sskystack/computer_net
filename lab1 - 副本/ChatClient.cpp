#include <iostream>
#include <winsock2.h>
#include <thread>
#include <mutex>
#include <string>
#include <atomic>
#include <map>
#include <functional>
#include "protocol.h"
#include "utils.h"

#pragma comment(lib, "ws2_32.lib")

using namespace std;

// 全局变量
SOCKET g_server_socket = INVALID_SOCKET;
atomic<bool> g_running(false);
int g_user_id = -1;
string g_username;
mutex g_console_mutex;

// 函数声明
void receiveMessages();
void sendUserInput();
void displayMenu();
void handleServerMessage(const Message& msg);
bool sendChatMessage(const string& content);
bool sendCommand(MessageType type, const string& content = "");

// 处理服务器消息
void handleServerMessage(const Message& msg) {
    lock_guard<mutex> lock(g_console_mutex);
    cout << "\n";

    switch (msg.type) {
        case MSG_LOGIN:
            g_user_id = msg.user_id;
            cout << "[SYSTEM] Login successful! Your ID: " << g_user_id << endl;
            break;

        case MSG_CHAT:
            cout << "[" << msg.timestamp << "] "
                 << msg.username << ": " << msg.content << endl;
            break;

        case MSG_SYSTEM:
            cout << "[SYSTEM] " << msg.content << endl;
            break;

        case MSG_LIST:
            cout << "[USER LIST] " << msg.content << endl;
            break;

        default:
            break;
    }

    cout << "> ";
    cout.flush();
}

// 发送命令消息
bool sendCommand(MessageType type, const string& content) {
    Message msg(type, g_username, content);
    msg.user_id = g_user_id;
    msg.timestamp = getCurrentTime();
    return sendMessage(g_server_socket, JsonMessage::serialize(msg));
}

// 线程:接收来自服务器的消息
void receiveMessages() {
    string message;
    while (g_running) {
        if (!recvMessage(g_server_socket, message)) {
            if (g_running) {
                lock_guard<mutex> lock(g_console_mutex);
                cout << "\n[ERROR] Connection to server lost" << endl;
                g_running = false;
            }
            break;
        }

        Message msg = JsonMessage::deserialize(message);
        handleServerMessage(msg);
    }
}

// 线程:获取用户输入并发送
void sendUserInput() {
    // 命令处理映射表
    static const map<string, function<bool()>> commands = {
        {"/quit", []() {
            sendCommand(MSG_LOGOUT, "");
            g_running = false;
            return false;  // 退出循环
        }},
        {"/exit", []() {
            sendCommand(MSG_LOGOUT, "");
            g_running = false;
            return false;  // 退出循环
        }},
        {"/list", []() {
            return sendCommand(MSG_LIST, "");
        }},
        {"/help", []() {
            lock_guard<mutex> lock(g_console_mutex);
            displayMenu();
            return true;  // 继续循环
        }}
    };

    string input;
    while (g_running) {
        cout << "> ";
        if (!getline(cin, input)) {
            g_running = false;
            break;
        }

        if (input.empty()) {
            continue;
        }

        // 检查是否为命令
        auto cmd_it = commands.find(input);
        if (cmd_it != commands.end()) {
            if (!cmd_it->second()) {
                break;  // 命令要求退出
            }
        } else {
            // 发送聊天消息
            if (!sendCommand(MSG_CHAT, input)) {
                g_running = false;
                break;
            }
        }
    }
}

void displayMenu() {
    cout << "\n========== Chat Commands ==========" << endl;
    cout << "/list  - Show online users" << endl;
    cout << "/help  - Show this help" << endl;
    cout << "/quit  - Exit the chat" << endl;
    cout << "==================================\n" << endl;
}

// 获取用户输入(带提示和默认值)
string getUserInput(const string& prompt, const string& default_value = "") {
    cout << prompt;
    string input;
    getline(cin, input);
    return input.empty() ? default_value : input;
}

// 尝试将字符串转换为整数
int tryParseInt(const string& str, int default_value) {
    try {
        return stoi(str);
    } catch (...) {
        return default_value;
    }
}

int main() {
    // 设置控制台UTF-8编码
    setConsoleUTF8();

    // 获取服务器地址和用户名
    cout << "======== Chat Client ========" << endl;
    string server_addr = getUserInput("Server address (default: localhost): ", "localhost");
    int port = tryParseInt(getUserInput("Server port (default: " +
                          to_string(DEFAULT_PORT) + "): "), DEFAULT_PORT);
    g_username = getUserInput("Username: ", "User");
    cout << "===========================\n" << endl;

    // 初始化Winsock
    if (!initializeWinsock()) {
        cerr << "Failed to initialize Winsock" << endl;
        return 1;
    }

    // 创建Socket
    g_server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (g_server_socket == INVALID_SOCKET) {
        cerr << "Socket creation failed: " << WSAGetLastError() << endl;
        cleanupWinsock();
        return 1;
    }

    // 解析服务器地址
    hostent* host = gethostbyname(server_addr.c_str());
    if (host == nullptr) {
        cerr << "Failed to resolve server address: " << server_addr << endl;
        closeSocket(g_server_socket);
        cleanupWinsock();
        return 1;
    }

    // 连接到服务器
    sockaddr_in server_socket_addr = {};
    server_socket_addr.sin_family = AF_INET;
    server_socket_addr.sin_addr.s_addr = *reinterpret_cast<u_long*>(host->h_addr_list[0]);
    server_socket_addr.sin_port = htons(static_cast<u_short>(port));

    if (connect(g_server_socket, reinterpret_cast<sockaddr*>(&server_socket_addr),
                sizeof(server_socket_addr)) == SOCKET_ERROR) {
        cerr << "Connection to server failed: " << WSAGetLastError() << endl;
        closeSocket(g_server_socket);
        cleanupWinsock();
        return 1;
    }

    cout << "Connected to server at " << server_addr << ":" << port << endl;
    cout << "Type /help for commands\n" << endl;

    // 发送登录消息
    Message login_msg(MSG_LOGIN, g_username, "");
    if (!sendMessage(g_server_socket, JsonMessage::serialize(login_msg))) {
        cerr << "Failed to send login message" << endl;
        closeSocket(g_server_socket);
        cleanupWinsock();
        return 1;
    }

    g_running = true;

    // 启动接收线程
    thread recv_thread(receiveMessages);
    recv_thread.detach();

    // 主线程处理用户输入
    sendUserInput();

    // 等待接收线程完成
    this_thread::sleep_for(chrono::milliseconds(100));

    // 清理资源
    closeSocket(g_server_socket);
    cleanupWinsock();

    cout << "Bye!\n" << endl;

    return 0;
}
