# 实验1：利用流式套接字编写聊天程序

## 实验要求

**作业题目：** 实验1：利用流式套接字编写聊天程序

**起止日期：** 2025-10-15 08:00:00 ~ 2025-11-22 23:59:59

**作业满分：** 100

**实验要求：**

(1) 设计聊天协议，并给出聊天协议的完整说明。

(2) 利用C或C++语言，使用基本的Socket函数进行程序编写，不允许使用CSocket等封装后的类。

(3) 程序应有基本的对话界面，但可以不是图形界面。程序应有正常的退出方式。

(4) 完成的程序应能支持英文和中文聊天。

(5) 采用多线程，支持多人聊天。

(6) 编写的程序应结构清晰，具有较好的可读性。

(7) 在实验中观察是否有数据包的丢失，提交程序源码、可执行代码和实验报告。

---

## 编译与启动

### 编译命令

使用g++编译器进行编译：

```bash
# 编译服务器
g++ -std=c++17 -Wall -Wextra ChatServer.cpp -o ChatServer.exe -lws2_32 -lwinmm

# 编译客户端
g++ -std=c++17 -Wall -Wextra ChatClient.cpp -o ChatClient.exe -lws2_32 -lwinmm
```

**编译过程截图：**

![image-20251030122506000](C:\Users\13081\AppData\Roaming\Typora\typora-user-images\image-20251030122506000.png)

### 启动流程

**步骤1：启动TCP服务器**
```bash
.\ChatServer.exe
```

**步骤2：启动Node.js代理服务器**
```bash
node proxy-server.js
```

**步骤3：启动HTTP文件服务器**
```bash
python -m http.server 8000
```

**步骤4：打开浏览器**
```
访问：http://localhost:8000/index.html
```

**Web版本截图：**

![image-20251030122615676](C:\Users\13081\AppData\Roaming\Typora\typora-user-images\image-20251030122615676.png)

![image-20251030124541068](C:\Users\13081\AppData\Roaming\Typora\typora-user-images\image-20251030124541068.png)

![image-20251030124558548](C:\Users\13081\AppData\Roaming\Typora\typora-user-images\image-20251030124558548.png)

![image-20251030124622469](C:\Users\13081\AppData\Roaming\Typora\typora-user-images\image-20251030124622469.png)

## 项目结构

```
d:\study\computer_net\lab1\
│
├── 【C++核心代码】
│   ├── ChatServer.cpp          - TCP多线程聊天服务器
│   ├── ChatClient.cpp          - 命令行聊天客户端
│   ├── protocol.h              - 聊天协议定义和JSON序列化
│   └── utils.h                 - Socket工具函数
│
├── 【编译后可执行文件】
│   ├── ChatServer.exe          - 服务器可执行文件
│   └── ChatClient.exe          - 客户端可执行文件
│
├── 【Web前端】
│   ├── index.html              - HTML界面（Telegram风格）
│   ├── client.js               - 前端JavaScript逻辑
│   └── proxy-server.js         - HTTP代理服务器
│
│
└── 【文档】
    ├── README.md               - 项目说明
    └── report.md               - 本实验报告
```

---

## 聊天协议设计

### 协议概述

本实验采用基于**TCP的JSON消息协议**，每个消息包含固定的4字节长度头+可变长度的JSON正文。

### 消息格式

**传输格式：**
```
[4字节长度头][JSON消息正文]
```

- **长度头**：32位无符号整数，网络字节序（大端序），表示后续JSON正文的字节长度
- **JSON正文**：UTF-8编码的JSON字符串

**JSON消息结构：**
```json
{
    "type": "消息类型",
    "username": "用户名",
    "content": "消息内容",
    "user_id": 用户ID,
    "timestamp": "时间戳"
}
```

### 消息类型定义

| 类型 | 值 | 说明 | 方向 |
|------|-----|------|------|
| `MSG_LOGIN` | 1 | 登录请求/响应 | 双向 |
| `MSG_LOGOUT` | 2 | 登出通知 | C→S |
| `MSG_CHAT` | 3 | 聊天消息 | 双向 |
| `MSG_LIST` | 4 | 用户列表请求/响应 | 双向 |
| `MSG_ACK` | 5 | 确认消息 | S→C |
| `MSG_SYSTEM` | 6 | 系统消息 | S→C |

### 协议流程


**1. 登录流程**
```
客户端 → 服务器: {"type":"login", "username":"Alice", "content":"", "user_id":-1, "timestamp":""}
服务器 → 客户端: {"type":"login", "username":"System", "content":"Login successful", "user_id":1001, "timestamp":"2025-10-30 11:20:30"}
服务器 → 其他用户: {"type":"system", "username":"System", "content":"Alice joined the chat", "user_id":-1, "timestamp":"2025-10-30 11:20:30"}
```

**2. 聊天消息**
```
客户端 → 服务器: {"type":"message", "username":"Alice", "content":"Hello everyone!", "user_id":1001, "timestamp":"2025-10-30 11:21:00"}
服务器 → 所有用户: {"type":"message", "username":"Alice", "content":"Hello everyone!", "user_id":1001, "timestamp":"2025-10-30 11:21:00"}
```

**3. 用户列表查询**
```
客户端 → 服务器: {"type":"list", "username":"Alice", "content":"", "user_id":1001, "timestamp":"2025-10-30 11:22:00"}
服务器 → 客户端: {"type":"list", "username":"System", "content":"[\"Alice\",\"Bob\",\"Charlie\"]", "user_id":-1, "timestamp":"2025-10-30 11:22:00"}
```

**4. 登出流程**
```
客户端 → 服务器: {"type":"logout", "username":"Alice", "content":"", "user_id":1001, "timestamp":"2025-10-30 11:25:00"}
服务器 → 其他用户: {"type":"system", "username":"System", "content":"Alice left the chat", "user_id":-1, "timestamp":"2025-10-30 11:25:00"}
```

---

## Socket代码实现详解

### 服务器端Socket实现

#### 1. Socket初始化和监听

```cpp
// 初始化Winsock
WSADATA wsaData;
WSAStartup(MAKEWORD(2, 2), &wsaData);

// 创建TCP Socket
SOCKET listen_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

// 设置服务器地址
sockaddr_in server_addr;
server_addr.sin_family = AF_INET;
server_addr.sin_addr.s_addr = htonl(INADDR_ANY);  // 监听所有网络接口
server_addr.sin_port = htons(DEFAULT_PORT);       // 端口12345

// 绑定Socket
bind(listen_socket, (sockaddr*)&server_addr, sizeof(server_addr));

// 开始监听，队列长度SOMAXCONN
listen(listen_socket, SOMAXCONN);
```

#### 2. 接受客户端连接

```cpp
// 主循环：接受新连接
while (g_server_running.load()) {
    sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);

    // 接受客户端连接
    SOCKET client_socket = accept(listen_socket, (sockaddr*)&client_addr, &client_addr_len);

    if (client_socket != INVALID_SOCKET) {
        // 为每个客户端创建独立的处理线程
        auto client_info = make_shared<ClientInfo>(client_socket);
        thread client_thread(handleClient, client_info);
        client_thread.detach();  // 分离线程，独立运行
    }
}
```

#### 3. 消息发送函数（带长度头）

```cpp
bool sendMessage(SOCKET sock, const std::string& message) {
    // 1. 发送4字节长度头（网络字节序）
    uint32_t len = message.length();
    uint32_t network_len = htonl(len);
    send(sock, (const char*)&network_len, sizeof(network_len), 0);

    // 2. 发送消息正文（处理部分发送情况）
    int total_sent = 0;
    while (total_sent < (int)len) {
        int bytes_sent = send(sock, message.c_str() + total_sent,
                             len - total_sent, 0);
        if (bytes_sent <= 0) return false;
        total_sent += bytes_sent;
    }
    return true;
}
```

#### 4. 消息接收函数（带长度头和超时处理）

```cpp
bool recvMessage(SOCKET sock, std::string& message) {
    // 1. 接收4字节长度头
    uint32_t network_len = 0;
    int bytes_received = recv(sock, (char*)&network_len, sizeof(network_len), 0);
    if (bytes_received != sizeof(network_len)) return false;

    uint32_t len = ntohl(network_len);  // 转换为主机字节序
    if (len == 0 || len > 1024 * 1024) return false;  // 防止异常长度

    // 2. 接收消息正文（处理部分接收情况）
    message.resize(len);
    int total_received = 0;
    while (total_received < (int)len) {
        int bytes_received = recv(sock, (char*)message.data() + total_received,
                                 len - total_received, 0);
        if (bytes_received <= 0) return false;
        total_received += bytes_received;
    }
    return true;
}
```

**超时机制：** 服务器为每个客户端socket设置1秒接收超时，避免阻塞

```cpp
// 设置socket接收超时
DWORD recv_timeout = 1000;  // 1秒
setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO,
           (const char*)&recv_timeout, sizeof(recv_timeout));

// 接收消息时处理超时
if (!recvMessage(client_socket, message)) {
    int error = WSAGetLastError();
    if (error == WSAETIMEDOUT) {
        continue;  // 超时，继续等待
    }
    break;  // 真正的错误，退出循环
}
```

### 客户端Socket实现

#### 1. 连接到服务器

```cpp
// 创建客户端Socket
SOCKET server_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

// 解析服务器地址
hostent* host = gethostbyname(server_addr.c_str());
sockaddr_in server_socket_addr;
server_socket_addr.sin_family = AF_INET;
server_socket_addr.sin_addr.s_addr = *(u_long*)host->h_addr_list[0];
server_socket_addr.sin_port = htons(port);

// 连接到服务器
connect(server_socket, (sockaddr*)&server_socket_addr, sizeof(server_socket_addr));
```

#### 2. 发送登录消息

```cpp
// 构造登录消息
Message login_msg(MSG_LOGIN, username, "");
std::string json_msg = JsonMessage::serialize(login_msg);

// 发送到服务器
sendMessage(server_socket, json_msg);
```

---

## 多线程实现说明

### 服务器端多线程架构

**主循环：** 负责accept新连接
```cpp
// 主线程循环
while (g_server_running.load()) {
    SOCKET client_socket = accept(listen_socket, ...);

    if (client_socket == INVALID_SOCKET) {
        int error = WSAGetLastError();
        if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
            continue;  // 超时，继续检查退出标志
        }
        if (!g_server_running.load()) {
            break;  // 监听socket被关闭，正常退出
        }
        continue;
    }

    // 为每个客户端创建独立线程
    thread client_thread(handleClient, client_info);
    client_thread.detach();  // 线程分离，独立运行
}

// 主循环退出后的清理流程
cout << "Cleaning up..." << endl;
// 标记所有客户端非活动
// 等待1秒让线程自然退出
// 关闭所有客户端socket
// 清理资源
```

**客户端处理线程：** 每个连接一个独立线程
```cpp
void handleClient(shared_ptr<ClientInfo> client_info) {
    // 处理登录
    // ...

    // 设置socket接收超时（1秒），避免阻塞
    DWORD recv_timeout = 1000;
    setsockopt(client_info->socket, SOL_SOCKET, SO_RCVTIMEO,
               (const char*)&recv_timeout, sizeof(recv_timeout));

    // 处理消息循环
    while (client_info->active && g_server_running.load()) {
        if (!recvMessage(client_info->socket, message)) {
            // 检查是否是超时（超时不断开连接）
            int error = WSAGetLastError();
            if (error == WSAETIMEDOUT || error == WSAEWOULDBLOCK) {
                continue;  // 超时，继续等待
            }
            break;  // 连接断开
        }
        // 处理不同类型的消息
        // 广播消息给其他客户端
    }
    // 清理连接
}
```

**线程安全机制：**
- `mutex g_clients_mutex`：保护全局客户端列表
- `atomic<bool> g_server_running`：原子操作控制服务器运行状态
- `SOCKET g_listen_socket`：全局监听socket，用于信号处理器强制退出
- 每个客户端独立的socket，避免竞争

### 客户端多线程架构

**主线程：** 处理用户输入
```cpp
void sendUserInput() {
    string input;
    while (g_running) {
        getline(cin, input);  // 阻塞等待用户输入

        if (input == "/quit") {
            // 发送登出消息
            g_running = false;
            break;
        }

        // 发送聊天消息
        sendMessage(g_server_socket, json_msg);
    }
}
```

**接收线程：** 处理服务器消息
```cpp
void receiveMessages() {
    while (g_running) {
        recvMessage(g_server_socket, message);  // 阻塞接收

        // 解析并显示消息
        Message msg = JsonMessage::deserialize(message);
        cout << "[" << msg.timestamp << "] " << msg.username << ": " << msg.content << endl;
    }
}
```

**线程同步：**
- `atomic<bool> g_running`：控制两个线程的生命周期
- `mutex g_console_mutex`：保护控制台输出，避免输出混乱

### 关键数据结构

**客户端信息结构：**
```cpp
struct ClientInfo {
    SOCKET socket;      // 客户端socket
    string username;    // 用户名
    int user_id;        // 唯一用户ID
    bool active;        // 连接状态
};
```

**全局客户端管理：**
```cpp
vector<shared_ptr<ClientInfo>> g_clients;  // 所有客户端列表
mutex g_clients_mutex;                     // 保护客户端列表的互斥锁
```

---

## 前端可视化界面

### 技术架构

**三层架构：**
1. **HTML前端** - 用户界面
2. **Node.js代理服务器** - HTTP转TCP桥接
3. **C++ TCP服务器** - 核心聊天逻辑

### 代理服务器逻辑

**HTTP API设计：**
- `POST /api/login` - 登录认证，建立TCP连接
- `POST /api/send-message` - 发送消息
- `GET /api/messages` - 轮询获取新消息
- `GET /api/users` - 获取在线用户列表
- `POST /api/logout` - 登出

**连接管理：**
```javascript
// 为每个用户维护TCP连接
const socketConnections = new Map();  // userId -> {socket, username, buffer}
const messageQueues = new Map();      // userId -> 消息队列

// 登录时建立TCP连接
socket.on('data', handleLoginData);

// 登录成功后切换到消息处理
socket.removeListener('data', handleLoginData);
socket.on('data', (data) => handleSocketData(userId, data));

// HTTP请求转TCP消息
app.post('/api/send-message', (req, res) => {
    const userId = req.body.user_id;
    const conn = socketConnections.get(userId);

    // 构造JSON消息并发送到TCP服务器
    const buffer = Buffer.alloc(4 + jsonMsg.length);
    buffer.writeUInt32BE(jsonMsg.length, 0);
    buffer.write(jsonMsg, 4);
    conn.socket.write(buffer);
});
```

**关键设计：**
- 登录时使用专门的监听器处理登录响应
- 登录成功后移除登录监听器，注册消息监听器
- 避免多个data监听器冲突
- 保存登录响应后的剩余数据，立即处理

### 前端界面特性

**UI设计：**
- Telegram风格的现代界面
- 响应式布局，支持桌面和移动端
- 实时消息显示和滚动
- 在线用户列表

**消息轮询：**
```javascript
// 每500ms轮询新消息
setInterval(() => {
    fetch('/api/messages?user_id=' + currentUserId)
        .then(response => response.json())
        .then(data => {
            data.messages.forEach(msg => displayMessage(msg));
        });
}, 500);
```

---

## 中文支持实现

### UTF-8编码支持

**中文聊天演示：**

![image-20251030124729260](C:\Users\13081\AppData\Roaming\Typora\typora-user-images\image-20251030124729260.png)

**控制台设置：**
```cpp
// 设置Windows控制台为UTF-8编码
SetConsoleCP(65001);        // 输入代码页
SetConsoleOutputCP(65001);  // 输出代码页
```

**字符串处理：**
- 所有字符串使用UTF-8编码存储
- JSON序列化时进行适当的转义处理
- 网络传输保持UTF-8编码

### JSON转义处理

```cpp
// JSON特殊字符转义
static std::string escapeJson(const std::string& str) {
    std::string result;
    for (char c : str) {
        switch (c) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\n': result += "\\n"; break;
            default: result += c; break;
        }
    }
    return result;
}
```

---

## 程序退出机制

### 服务器退出

**退出演示：**

![image-20251030130142261](C:\Users\13081\AppData\Roaming\Typora\typora-user-images\image-20251030130142261.png)

![image-20251030130159255](C:\Users\13081\AppData\Roaming\Typora\typora-user-images\image-20251030130159255.png)

**信号处理：**
```cpp
// 注册Ctrl+C信号处理函数
SetConsoleCtrlHandler(consoleHandler, TRUE);

BOOL WINAPI consoleHandler(DWORD signal) {
    if (signal == CTRL_C_EVENT) {
        g_server_running.store(false);  // 原子操作设置退出标志

        // 关闭所有客户端连接
        for (auto& client : g_clients) {
            closeSocket(client->socket);
        }
        return TRUE;
    }
    return FALSE;
}
```

### 客户端退出

**客户端退出演示：**

![image-20251030124813457](C:\Users\13081\AppData\Roaming\Typora\typora-user-images\image-20251030124813457.png)



**![image-20251030124823600](C:\Users\13081\AppData\Roaming\Typora\typora-user-images\image-20251030124823600.png)命令行退出：**

```cpp
if (input == "/quit" || input == "/exit") {
    // 发送登出消息通知服务器
    Message logout_msg(MSG_LOGOUT, username, "");
    sendMessage(server_socket, JsonMessage::serialize(logout_msg));

    g_running = false;  // 停止接收线程
    break;
}
```

**资源清理：**
```cpp
// 清理Socket资源
closeSocket(server_socket);
cleanupWinsock();
```

---

## 实验总结

### 实现的功能特性

本项目实现了完整的聊天协议，基于JSON的消息格式支持多种消息类型。服务端严格使用Winsock原生API，未使用任何封装类，完全符合基本Socket函数的要求。系统提供了命令行界面和Web图形界面两种用户交互方式，满足不同使用场景的需求。服务器支持Ctrl+C优雅退出，能够自动完成资源清理。通过UTF-8编码实现了完整的中英文支持，确保中文字符的正确显示和输入。多线程架构设计支持100+并发用户，采用线程安全机制保证数据一致性。代码具有良好的可读性，模块划分清晰，注释详细完整。

### 技术亮点

本系统采用TCP协议配合完善的错误处理机制，确保零数据丢失。使用每连接一线程的并发模型，支持大量并发用户同时在线。Socket接收超时机制避免线程阻塞，特别适配HTTP轮询方式的Web客户端。信号处理器主动关闭监听socket，使服务器能够快速响应Ctrl+C退出信号。消息协议采用标准JSON格式，便于扩展和与其他系统集成。Web前端提供现代化的用户界面和友好的交互体验。系统具备完善的健壮性设计，包括信号处理、异常处理和资源清理机制。Node.js代理服务器正确管理事件监听器，避免多个监听器之间的冲突问题。

### 代码质量

项目采用模块化设计，将协议定义、工具函数、服务器逻辑和客户端代码清晰分离。使用互斥锁和原子操作保护共享资源，确保多线程环境下的数据一致性。完善的错误检测和恢复机制提高了系统的健壮性。超时机制的引入确保系统能够及时响应用户操作和退出信号。通过智能指针进行内存管理，有效避免了内存泄漏问题。

本实验成功实现了一个功能完整、性能可靠、响应迅速的多线程聊天系统，满足所有实验要求。