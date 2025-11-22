// ==================== 全局变量 ====================
let serverAddress = 'localhost';
let serverPort = 12345;
let currentUsername = '';
let currentUserId = -1;
let isConnected = false;
let messagePollingInterval = null;
let userListInterval = null;
let selectedUser = null;
let isConnecting = false;

// ==================== 登录处理 ====================
function handleLogin(event) {
    event.preventDefault();

    // 防止重复连接
    if (isConnecting) {
        alert('正在连接中，请稍候...');
        return;
    }

    const addr = document.getElementById('serverAddr').value.trim();
    const port = document.getElementById('serverPort').value.trim();
    const username = document.getElementById('username').value.trim();

    if (!addr) {
        alert('请输入服务器地址');
        return;
    }
    if (!port) {
        alert('请输入服务器端口');
        return;
    }
    if (!username) {
        alert('请输入用户名');
        return;
    }

    serverAddress = addr;
    serverPort = parseInt(port);
    currentUsername = username;

    connectToServer();
}

// ==================== 连接服务器 ====================
async function connectToServer() {
    isConnecting = true;
    try {
        console.log('正在连接到服务器...', {
            server_address: serverAddress,
            server_port: serverPort,
            username: currentUsername
        });

        const response = await fetch('http://localhost:3000/api/login', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                server_address: serverAddress,
                server_port: serverPort,
                username: currentUsername
            })
        });

        console.log('收到响应:', response.status);

        if (!response.ok) {
            throw new Error('HTTP ' + response.status);
        }

        const result = await response.json();
        console.log('响应内容:', result);

        if (result.success) {
            currentUserId = result.user_id;
            isConnected = true;

            console.log('登录成功! user_id:', currentUserId);

            // 隐藏登录页，显示应用
            const loginScreen = document.getElementById('loginScreen');
            const appContainer = document.getElementById('appContainer');

            console.log('切换界面...');
            console.log('loginScreen:', loginScreen);
            console.log('appContainer:', appContainer);

            if (loginScreen) {
                loginScreen.style.display = 'none';
            }
            if (appContainer) {
                appContainer.classList.add('active');
                console.log('appContainer class:', appContainer.className);
            }

            // 显示当前用户名
            const currentUserNameElement = document.getElementById('currentUserName');
            if (currentUserNameElement) {
                currentUserNameElement.textContent = currentUsername;
            }

            // 登录成功后直接显示聊天界面
            document.getElementById('chatHeader').style.display = 'flex';
            document.getElementById('inputArea').style.display = 'flex';
            document.getElementById('chatHeaderName').textContent = '聊天室';

            // 启动轮询
            startMessagePolling();
            startUserListPolling();

            console.log('轮询已启动');
        } else {
            alert(result.message || '连接失败');
        }
    } catch (error) {
        console.error('连接错误:', error);
        alert('连接错误: ' + error.message);
    } finally {
        isConnecting = false;
    }
}

// ==================== 发送消息 ====================
function handleInputKeyPress(event) {
    if (event.key === 'Enter' && !event.shiftKey) {
        event.preventDefault();
        sendMessage();
    }
}

async function sendMessage() {
    const input = document.getElementById('messageInput');
    const message = input.value.trim();

    if (!message) {
        return;
    }

    if (!isConnected) {
        alert('未连接到服务器');
        return;
    }

    try {
        const response = await fetch('http://localhost:3000/api/send-message', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                user_id: currentUserId,
                username: currentUsername,
                content: message
            })
        });

        if (response.ok) {
            input.value = '';
            // 清空输入框后，消息会通过轮询从服务器获取并显示
            // 不在这里立即显示，避免消息重复
        }
    } catch (error) {
        console.error('发送错误:', error);
    }
}

// ==================== 消息轮询 ====================
function startMessagePolling() {
    fetchMessages();
    messagePollingInterval = setInterval(fetchMessages, 500);
}

async function fetchMessages() {
    if (!isConnected) return;

    try {
        const response = await fetch('http://localhost:3000/api/messages?user_id=' + currentUserId);
        if (response.ok) {
            const result = await response.json();
            if (result.success && result.messages) {
                result.messages.forEach(msg => {
                    displayMessage(msg);
                });
            }
        }
    } catch (error) {
        console.error('获取消息错误:', error);
    }
}

// ==================== 显示消息 ====================
let displayedMessages = new Set();
function displayMessage(msg, isOwn = false) {
    const messageKey = `${msg.timestamp}-${msg.username}-${msg.content}-${msg.type}`;

    // 对所有消息进行去重检查（不仅仅是isOwn=false）
    if (displayedMessages.has(messageKey)) {
        return;
    }
    displayedMessages.add(messageKey);

    // 如果收到的是logout消息类型，检查是否是当前用户的logout
    // 如果是当前用户的logout，说明是从其他地方登出的，需要返回登录页
    if (msg.type === 'logout' && msg.username === currentUsername) {
        console.log('检测到当前用户从其他地方登出，返回登录页');
        // 不显示这条消息，直接退出
        performLogout();
        return;
    }

    // 如果是其他用户的logout消息，服务器会发送system消息通知，忽略logout类型消息
    if (msg.type === 'logout') {
        return;
    }

    const messagesContainer = document.getElementById('messagesContainer');

    // 清空初始欢迎消息
    const emptyChat = messagesContainer.querySelector('.empty-chat');
    if (emptyChat) {
        messagesContainer.innerHTML = '';
    }

    const messageEl = document.createElement('div');
    messageEl.className = `message ${isOwn || msg.username === currentUsername ? 'own' : 'other'}`;

    if (msg.type === 'system' || msg.username === 'System') {
        // 系统消息
        messageEl.innerHTML = `
            <div class="message-content">
                <div class="message-text system">${escapeHtml(msg.content)}</div>
            </div>
        `;
    } else {
        // 普通消息
        const isOwnMsg = isOwn || msg.username === currentUsername;

        if (!isOwnMsg) {
            messageEl.innerHTML = `
                <div class="message-avatar">${msg.username.charAt(0).toUpperCase()}</div>
                <div class="message-content">
                    <div class="message-name">${escapeHtml(msg.username)}</div>
                    <div class="message-text">${escapeHtml(msg.content)}</div>
                    <div class="message-time">${msg.timestamp || formatTime(new Date())}</div>
                </div>
            `;
        } else {
            messageEl.innerHTML = `
                <div class="message-content">
                    <div class="message-name own-name">${escapeHtml(msg.username)}</div>
                    <div class="message-text">${escapeHtml(msg.content)}</div>
                    <div class="message-time">${msg.timestamp || formatTime(new Date())}</div>
                </div>
            `;
        }
    }

    messagesContainer.appendChild(messageEl);

    // 自动滚动到底部
    setTimeout(() => {
        messagesContainer.scrollTop = messagesContainer.scrollHeight;
    }, 0);
}

// ==================== 用户列表 ====================
function startUserListPolling() {
    fetchUserList();
    userListInterval = setInterval(fetchUserList, 1000);
}

async function fetchUserList() {
    if (!isConnected) return;

    try {
        const response = await fetch('http://localhost:3000/api/users');
        if (response.ok) {
            const result = await response.json();
            if (result.success && result.users) {
                displayUserList(result.users);
            }
        }
    } catch (error) {
        console.error('获取用户列表错误:', error);
    }
}

// ==================== 显示用户列表 ====================
let lastUserList = [];
function displayUserList(users) {
    const userListJson = JSON.stringify(users.sort());
    const lastUserListJson = JSON.stringify(lastUserList.sort());

    if (userListJson === lastUserListJson) {
        return;
    }

    lastUserList = users;
    const chatsList = document.getElementById('chatsList');
    chatsList.innerHTML = '';

    users.forEach(user => {
        // 改为div而不是button，只显示用户列表，不可点击
        const chatItem = document.createElement('div');
        chatItem.className = `chat-item ${user === currentUsername ? 'current-user' : ''}`;

        chatItem.innerHTML = `
            <div class="chat-avatar">${user.charAt(0).toUpperCase()}</div>
            <div class="chat-info">
                <div class="chat-name">${escapeHtml(user)}</div>
                <div class="chat-preview">在线</div>
            </div>
        `;

        chatsList.appendChild(chatItem);
    });
}

// ==================== 退出登录 ====================
async function logout() {
    if (!confirm('确定要退出登录吗？')) {
        return;
    }

    try {
        await fetch('http://localhost:3000/api/logout', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify({
                user_id: currentUserId,
                username: currentUsername
            })
        });
    } catch (error) {
        console.error('登出错误:', error);
    }

    performLogout();
}

// 执行登出操作（清理状态并返回登录页）
function performLogout() {
    // 停止轮询
    if (messagePollingInterval) clearInterval(messagePollingInterval);
    if (userListInterval) clearInterval(userListInterval);

    // 重置状态
    isConnected = false;
    currentUsername = '';
    currentUserId = -1;
    selectedUser = null;
    displayedMessages.clear();
    lastUserList = [];

    // 清空用户名显示
    const currentUserNameElement = document.getElementById('currentUserName');
    if (currentUserNameElement) {
        currentUserNameElement.textContent = '';
    }

    // 显示登录页
    document.getElementById('loginScreen').style.display = 'flex';
    document.getElementById('appContainer').classList.remove('active');
    document.getElementById('messagesContainer').innerHTML = `
        <div class="empty-chat">
            <div class="empty-icon">✈️</div>
            <div class="empty-text">选择一个聊天开始消息</div>
        </div>
    `;
}

// ==================== 工具函数 ====================
function formatTime(date) {
    const hours = String(date.getHours()).padStart(2, '0');
    const minutes = String(date.getMinutes()).padStart(2, '0');
    const seconds = String(date.getSeconds()).padStart(2, '0');
    return `${hours}:${minutes}:${seconds}`;
}

function escapeHtml(text) {
    const div = document.createElement('div');
    div.textContent = text;
    return div.innerHTML;
}

// ==================== 页面加载 ====================
document.addEventListener('DOMContentLoaded', function() {
    // 添加退出按钮事件监听
    const logoutBtns = document.querySelectorAll('[onclick="logout()"]');
    if (logoutBtns.length === 0) {
        // 如果页面中没有退出按钮，可以在此添加快捷键
        document.addEventListener('keydown', (e) => {
            // 可以添加快捷键，如 Ctrl+Q 退出
            if (e.ctrlKey && e.key === 'q') {
                logout();
            }
        });
    }
});

// ==================== 页面离开时清理 ====================
window.addEventListener('beforeunload', function(event) {
    if (isConnected) {
        // 使用同步的XMLHttpRequest确保登出请求能完成
        const xhr = new XMLHttpRequest();
        xhr.open('POST', 'http://localhost:3000/api/logout', false);  // 第三个参数false表示同步
        xhr.setRequestHeader('Content-Type', 'application/json');
        xhr.send(JSON.stringify({
            user_id: currentUserId,
            username: currentUsername
        }));
    }
    if (messagePollingInterval) clearInterval(messagePollingInterval);
    if (userListInterval) clearInterval(userListInterval);
});
