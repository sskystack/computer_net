/**
 * HTTP代理服务器
 * 作用：接收HTML前端的HTTP请求，转发给TCP聊天服务器
 * 运行：node proxy-server.js
 */

const http = require('http');
const url = require('url');
const net = require('net');

// 存储所有活跃的Socket连接
const socketConnections = new Map();

// 存储消息队列
const messageQueues = new Map();

// 存储用户信息
const users = new Map();

const PORT = 3000;

// 创建HTTP服务器
const server = http.createServer(handleRequest);

function handleRequest(req, res) {
    // CORS头
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');
    res.setHeader('Content-Type', 'application/json');

    if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
    }

    const parsedUrl = url.parse(req.url, true);
    const pathname = parsedUrl.pathname;
    const query = parsedUrl.query;

    console.log(`[${new Date().toLocaleTimeString()}] ${req.method} ${pathname}`);

    if (pathname === '/api/login' && req.method === 'POST') {
        handleLogin(req, res);
    } else if (pathname === '/api/send-message' && req.method === 'POST') {
        handleSendMessage(req, res);
    } else if (pathname === '/api/messages' && req.method === 'GET') {
        handleGetMessages(req, res, query);
    } else if (pathname === '/api/users' && req.method === 'GET') {
        handleGetUsers(req, res);
    } else if (pathname === '/api/logout' && req.method === 'POST') {
        handleLogout(req, res);
    } else {
        res.writeHead(404);
        res.end(JSON.stringify({ success: false, message: '不存在的API端点' }));
    }
}

// 读取请求体
function readBody(req, callback) {
    let body = '';
    req.on('data', chunk => {
        body += chunk.toString();
    });
    req.on('end', () => {
        try {
            const data = JSON.parse(body);
            callback(data);
        } catch (e) {
            callback(null);
        }
    });
}

// 处理登录
function handleLogin(req, res) {
    readBody(req, (data) => {
        if (!data || !data.username) {
            res.writeHead(400);
            res.end(JSON.stringify({ success: false, message: '缺少必要参数' }));
            return;
        }

        const serverAddr = data.server_address || 'localhost';
        const serverPort = data.server_port || 12345;
        const username = data.username;

        // 连接到TCP服务器
        const socket = net.createConnection(serverPort, serverAddr, () => {
            console.log(`✓ 已连接到 ${serverAddr}:${serverPort}`);

            // 发送登录消息
            const loginMsg = {
                type: 'login',
                username: username,
                content: '',
                timestamp: formatTime(new Date()),
                user_id: -1
            };

            const jsonMsg = JSON.stringify(loginMsg);
            const buffer = Buffer.alloc(4 + jsonMsg.length);
            buffer.writeUInt32BE(jsonMsg.length, 0);
            buffer.write(jsonMsg, 4);

            socket.write(buffer);
        });

        // 处理来自服务器的登录响应
        let loginBuffer = Buffer.alloc(0);
        let loginProcessed = false;

        // 定义data处理函数
        const handleLoginData = (data) => {
            console.log(`[LOGIN DEBUG] 收到数据，长度: ${data.length}, loginProcessed: ${loginProcessed}`);

            if (loginProcessed) {
                // 登录已处理，移除此监听器
                socket.removeListener('data', handleLoginData);
                console.log(`[LOGIN DEBUG] 登录已处理，移除监听器`);
                return;
            }

            loginBuffer = Buffer.concat([loginBuffer, data]);
            console.log(`[LOGIN DEBUG] 缓冲区总长度: ${loginBuffer.length}`);

            // 检查是否接收到完整的消息头
            if (loginBuffer.length < 4) return;

            const msgLen = loginBuffer.readUInt32BE(0);
            console.log(`[LOGIN DEBUG] 消息长度: ${msgLen}`);

            // 检查是否接收到完整的消息体
            if (loginBuffer.length < 4 + msgLen) return;

            try {
                const msgJson = loginBuffer.toString('utf8', 4, 4 + msgLen);
                console.log(`[LOGIN DEBUG] 解析到消息: ${msgJson}`);
                const msg = JSON.parse(msgJson);

                if (msg.type === 'login') {
                    loginProcessed = true;
                    const userId = msg.user_id;
                    console.log(`[LOGIN DEBUG] 登录成功，用户ID: ${userId}`);

                    // 移除登录监听器
                    socket.removeListener('data', handleLoginData);
                    console.log(`[LOGIN DEBUG] 已移除登录监听器`);

                    // 保存Socket连接
                    socketConnections.set(userId, {
                        socket: socket,
                        username: username,
                        buffer: loginBuffer.slice(4 + msgLen)  // 保存剩余数据
                    });
                    console.log(`[LOGIN DEBUG] 剩余数据长度: ${loginBuffer.length - 4 - msgLen}`);

                    // 初始化消息队列
                    messageQueues.set(userId, []);

                    // 存储用户信息
                    users.set(userId, username);

                    // 监听后续消息
                    console.log(`[LOGIN DEBUG] 注册后续消息监听器`);
                    socket.on('data', (data) => {
                        console.log(`[LOGIN DEBUG] 后续data监听器被触发`);
                        handleSocketData(userId, data);
                    });

                    socket.on('end', () => {
                        console.log(`✗ 客户端${userId}断开连接`);
                        socketConnections.delete(userId);
                        messageQueues.delete(userId);
                        users.delete(userId);
                    });

                    socket.on('error', (err) => {
                        console.error(`Socket错误(${userId}):`, err.message);
                        socketConnections.delete(userId);
                        messageQueues.delete(userId);
                        users.delete(userId);
                    });

                    // 如果登录响应后还有数据，立即处理
                    if (loginBuffer.length > 4 + msgLen) {
                        console.log(`[LOGIN DEBUG] 立即处理剩余数据`);
                        handleSocketData(userId, loginBuffer.slice(4 + msgLen));
                    }

                    // 返回成功响应
                    res.writeHead(200);
                    res.end(JSON.stringify({
                        success: true,
                        user_id: userId,
                        message: '登录成功'
                    }));
                }
            } catch (err) {
                console.error('登录响应解析错误:', err);
                console.error('接收到的数据:', loginBuffer.toString('hex'));
                loginProcessed = true;
                socket.removeListener('data', handleLoginData);
                res.writeHead(400);
                res.end(JSON.stringify({ success: false, message: '登录失败: ' + err.message }));
            }
        };

        // 注册登录数据监听器
        socket.on('data', handleLoginData);

        socket.on('error', (err) => {
            console.error('连接错误:', err.message);
            res.writeHead(500);
            res.end(JSON.stringify({ success: false, message: '无法连接到服务器: ' + err.message }));
        });

        socket.on('end', () => {
            console.log('服务器连接已关闭');
        });
    });
}

// 处理Socket数据
function handleSocketData(userId, data) {
    const conn = socketConnections.get(userId);
    if (!conn) {
        console.log(`[DEBUG] 未找到用户${userId}的连接`);
        return;
    }

    console.log(`[DEBUG] 收到用户${userId}的数据，长度: ${data.length}`);

    // 合并缓冲区
    conn.buffer = Buffer.concat([conn.buffer, data]);

    // 解析消息
    while (conn.buffer.length >= 4) {
        const msgLen = conn.buffer.readUInt32BE(0);
        console.log(`[DEBUG] 消息长度: ${msgLen}, 缓冲区长度: ${conn.buffer.length}`);

        if (conn.buffer.length < 4 + msgLen) {
            break; // 消息不完整，等待更多数据
        }

        try {
            const msgJson = conn.buffer.toString('utf8', 4, 4 + msgLen);
            console.log(`[DEBUG] 解析消息: ${msgJson}`);
            const msg = JSON.parse(msgJson);

            // 广播给所有客户端（除了logout消息，logout消息仅用于客户端向服务器发送）
            // 服务器会返回system类型的消息通知其他人用户已下线
            broadcastMessage(msg);
        } catch (err) {
            console.error('消息解析错误:', err);
        }

        // 移除已处理的消息
        conn.buffer = conn.buffer.slice(4 + msgLen);
    }
}

// 广播消息
function broadcastMessage(msg) {
    messageQueues.forEach((queue) => {
        queue.push(msg);
    });
}

// 处理发送消息
function handleSendMessage(req, res) {
    readBody(req, (data) => {
        if (!data || !data.user_id || !data.content) {
            res.writeHead(400);
            res.end(JSON.stringify({ success: false, message: '缺少必要参数' }));
            return;
        }

        const userId = data.user_id;
        const username = data.username;
        const content = data.content;

        const conn = socketConnections.get(userId);
        if (!conn) {
            res.writeHead(400);
            res.end(JSON.stringify({ success: false, message: '用户未连接' }));
            return;
        }

        // 构建聊天消息
        const chatMsg = {
            type: 'message',
            username: username,
            content: content,
            timestamp: formatTime(new Date()),
            user_id: userId
        };

        const jsonMsg = JSON.stringify(chatMsg);
        const buffer = Buffer.alloc(4 + jsonMsg.length);
        buffer.writeUInt32BE(jsonMsg.length, 0);
        buffer.write(jsonMsg, 4);

        conn.socket.write(buffer, (err) => {
            if (err) {
                console.error('发送消息错误:', err);
                res.writeHead(500);
                res.end(JSON.stringify({ success: false, message: '发送失败' }));
            } else {
                res.writeHead(200);
                res.end(JSON.stringify({ success: true, message: '发送成功' }));
            }
        });
    });
}

// 处理获取消息
function handleGetMessages(req, res, query) {
    const userId = parseInt(query.user_id);

    if (!userId || !messageQueues.has(userId)) {
        res.writeHead(400);
        res.end(JSON.stringify({ success: false, messages: [] }));
        return;
    }

    const messages = messageQueues.get(userId);
    messageQueues.set(userId, []); // 清空队列

    res.writeHead(200);
    res.end(JSON.stringify({ success: true, messages: messages }));
}

// 处理获取用户列表
function handleGetUsers(req, res) {
    const userList = Array.from(users.values());

    res.writeHead(200);
    res.end(JSON.stringify({ success: true, users: userList }));
}

// 处理登出
function handleLogout(req, res) {
    readBody(req, (data) => {
        if (!data || !data.user_id) {
            res.writeHead(400);
            res.end(JSON.stringify({ success: false, message: '缺少用户ID' }));
            return;
        }

        const userId = data.user_id;
        const conn = socketConnections.get(userId);

        if (conn) {
            // 发送登出消息
            const logoutMsg = {
                type: 'logout',
                username: conn.username,
                content: '',
                timestamp: formatTime(new Date()),
                user_id: userId
            };

            const jsonMsg = JSON.stringify(logoutMsg);
            const buffer = Buffer.alloc(4 + jsonMsg.length);
            buffer.writeUInt32BE(jsonMsg.length, 0);
            buffer.write(jsonMsg, 4);

            conn.socket.write(buffer, () => {
                conn.socket.end();
                socketConnections.delete(userId);
                messageQueues.delete(userId);
                users.delete(userId);

                res.writeHead(200);
                res.end(JSON.stringify({ success: true, message: '登出成功' }));
            });
        } else {
            res.writeHead(400);
            res.end(JSON.stringify({ success: false, message: '用户未连接' }));
        }
    });
}

// 格式化时间
function formatTime(date) {
    const hours = String(date.getHours()).padStart(2, '0');
    const minutes = String(date.getMinutes()).padStart(2, '0');
    const seconds = String(date.getSeconds()).padStart(2, '0');
    return `${hours}:${minutes}:${seconds}`;
}

// 启动服务器
server.listen(PORT, () => {
    console.log(`\n${'='.repeat(50)}`);
    console.log(`🚀 HTTP代理服务器已启动`);
    console.log(`📍 监听地址: http://localhost:${PORT}`);
    console.log(`${'='.repeat(50)}\n`);
});

// 优雅关闭
process.on('SIGINT', () => {
    console.log('\n关闭服务器...');
    // 关闭所有连接
    socketConnections.forEach(conn => {
        conn.socket.end();
    });
    server.close(() => {
        console.log('服务器已关闭');
        process.exit(0);
    });
});
