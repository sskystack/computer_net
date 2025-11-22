# 聊天程序使用说明

## 项目概述

这是一个基于Winsock2的多线程TCP聊天程序，支持多用户实时聊天、中文消息、以及基本的用户管理功能。

## 系统要求

- Windows操作系统（XP或更高版本）
- MinGW/g++ 编译器（用于编译）
- 至少20MB空闲磁盘空间

## 编译方法

### 方法1：使用PowerShell编译脚本（推荐）

```powershell
# 进入项目目录
cd D:\study\computer_net\lab1

# 执行编译脚本
.\build.ps1

# 输出：ChatServer.exe 和 ChatClient.exe
```

### 方法2：手动编译（仅使用g++）

```powershell
# 编译服务器
g++ -std=c++17 -Wall -Wextra ChatServer.cpp -o ChatServer.exe -lws2_32 -lwinmm

# 编译客户端
g++ -std=c++17 -Wall -Wextra ChatClient.cpp -o ChatClient.exe -lws2_32 -lwinmm
```

### 方法3：编译优化版本（添加优化选项）

```powershell
# 添加-O2优化
g++ -std=c++17 -O2 -Wall -Wextra ChatServer.cpp -o ChatServer.exe -lws2_32 -lwinmm
g++ -std=c++17 -O2 -Wall -Wextra ChatClient.cpp -o ChatClient.exe -lws2_32 -lwinmm
```

## 运行方法

### 第1步：启动服务器

打开一个PowerShell窗口：

```powershell
cd D:\study\computer_net\lab1
.\ChatServer.exe
```

输出示例：
```
Chat Server started on port 12345
Waiting for connections...
```

### 第2步：启动客户端

打开另一个（或多个）PowerShell窗口：

```powershell
cd D:\study\computer_net\lab1
.\ChatClient.exe
```

### 第3步：连接到服务器

按照提示输入以下信息：

```
======== Chat Client ========
Server address (default: localhost): localhost
Server port (default: 12345): 12345
Username: Alice
=============================
```

**参数说明**：
- **Server address**：默认localhost，可输入IP地址
- **Server port**：默认12345，需与服务器一致
- **Username**：任意用户名（支持中文，如"张三"）

### 第4步：聊天

输入消息并按Enter发送：

```
> Hello everyone
> 你好，各位！
```

## 命令参考

在客户端输入框中使用以下命令：

| 命令 | 功能 | 示例 |
|------|------|------|
| `/list` | 显示在线用户列表 | `/list` |
| `/help` | 显示帮助信息 | `/help` |
| `/quit` 或 `/exit` | 退出聊天程序 | `/quit` |
| 其他输入 | 发送聊天消息 | `Hello world!` |

## 测试场景

### 场景1：两个用户聊天

1. 启动服务器
2. 启动两个客户端（Alice和Bob）
3. Alice输入：`Hi Bob`
4. Bob应该收到Bob消息

### 场景2：多人群聊

1. 启动服务器
2. 启动3个以上客户端
3. 任意一个客户端发送消息
4. 所有客户端都应该收到该消息

### 场景3：中文消息测试

1. 任意客户端输入中文内容：`你好，世界！`
2. 所有客户端都应该正确显示中文消息

### 场景4：查看在线用户

任意客户端输入：`/list`

输出示例：
```
[USER LIST] ["Alice","Bob","Charlie"]
```

### 场景5：正常退出

任意客户端输入：`/quit` 或 `/exit`

该客户端应该优雅地断开连接，服务器显示：
```
User logged out: Alice
Client disconnected: Alice
```

## 故障排除

### 问题1：编译失败，提示"g++: command not found"

**解决方案**：
1. 安装MinGW：https://www.mingw-w64.org/
2. 将MinGW\bin目录添加到系统PATH环境变量
3. 重启PowerShell

### 问题2：运行时出现"Address already in use"错误

**解决方案**：
- 等待30秒让操作系统释放端口
- 或修改代码中的DEFAULT_PORT为其他端口

### 问题3：客户端无法连接到服务器

**检查清单**：
1. 确保服务器已启动且显示"Waiting for connections..."
2. 确保服务器地址正确（本地测试使用localhost）
3. 确保端口号一致（默认12345）
4. 检查防火墙设置，确保允许该端口通信

### 问题4：中文字符显示乱码

**解决方案**：
1. 确保PowerShell编码为UTF-8（默认应该支持）
2. 尝试右键点击窗口标题栏 -> 属性，设置字体为支持Unicode的字体（如Consolas）
3. 在PowerShell中执行：`chcp 65001`

### 问题5：消息发送后没有显示或显示延迟

**可能原因和解决方案**：
1. **网络连接问题**：检查网络是否正常
2. **服务器崩溃**：查看服务器窗口是否有错误信息
3. **输入缓冲**：确保按下了Enter键
4. **TCP延迟**：TCP协议的正常行为，通常毫秒级延迟

## 性能参数

基于当前实现的性能特性：

| 参数 | 值 |
|------|-----|
| 最大消息长度 | 1MB |
| 默认监听端口 | 12345 |
| TCP接收缓冲区 | 4096字节 |
| 最大并发连接 | 100+ (操作系统限制) |
| 消息格式 | JSON (UTF-8编码) |
| 传输协议 | TCP/IP |

## 源代码文件

```
lab1/
├── ChatServer.cpp          # 服务器程序（约200行）
├── ChatClient.cpp          # 客户端程序（约250行）
├── protocol.h              # 协议定义（约150行）
├── utils.h                 # 工具函数（约100行）
├── build.ps1               # 编译脚本
├── 实验报告.md              # 详细实验报告
└── README.md               # 本说明文档（此文件）
```

## 代码特点

1. **标准C++17**：使用现代C++特性
2. **无第三方库**：仅使用标准库和Winsock2
3. **多线程**：服务器为每个客户端创建独立线程
4. **JSON协议**：便于扩展和调试
5. **UTF-8编码**：完整的中文支持
6. **错误处理**：完善的异常和错误处理

## 注意事项

1. **安全性**：此程序用于教学目的，生产环境需添加加密和认证
2. **性能**：未进行大规模测试，可能需要优化
3. **跨平台**：当前仅支持Windows（Winsock2特定）
4. **端口占用**：确保12345端口未被其他程序占用

## 联系方式

如有问题或建议，请联系实验指导老师。

---

**最后更新**：2025-10-22
**版本**：1.0
**作者**：计算机网络课程实验
