#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <string>
#include <map>
#include <vector>
#include <cstring>

// 常量定义
#define DEFAULT_PORT 12345
#define BUFFER_SIZE 4096
#define MAX_USERNAME_LEN 32
#define MAX_USERS 100
#define MAX_MESSAGE_SIZE (1024 * 1024)  // 最大消息大小 1MB

// 消息类型
enum MessageType {
    MSG_LOGIN = 1,
    MSG_LOGOUT = 2,
    MSG_CHAT = 3,
    MSG_LIST = 4,
    MSG_ACK = 5,
    MSG_SYSTEM = 6
};

// 消息结构体
struct Message {
    MessageType type;
    std::string username;
    std::string content;
    std::string timestamp;
    int user_id;

    Message() : type(MSG_CHAT), user_id(-1) {}

    Message(MessageType t, const std::string& u, const std::string& c)
        : type(t), username(u), content(c), user_id(-1) {}
};

// JSON 序列化和反序列化类
class JsonMessage {
public:
    // 将Message序列化为JSON字符串
    static std::string serialize(const Message& msg) {
        std::string json = "{";
        json += "\"type\":\"" + getTypeString(msg.type) + "\",";
        json += "\"username\":\"" + escapeJson(msg.username) + "\",";
        json += "\"content\":\"" + escapeJson(msg.content) + "\",";
        json += "\"user_id\":" + std::to_string(msg.user_id) + ",";
        json += "\"timestamp\":\"" + msg.timestamp + "\"";
        json += "}";
        return json;
    }

    // 将JSON字符串反序列化为Message
    static Message deserialize(const std::string& json) {
        Message msg;
        msg.type = getTypeFromString(getValue(json, "type"));
        msg.username = unescapeJson(getValue(json, "username"));
        msg.content = unescapeJson(getValue(json, "content"));
        msg.timestamp = getValue(json, "timestamp");

        std::string user_id_str = getValue(json, "user_id");
        if (!user_id_str.empty()) {
            msg.user_id = std::stoi(user_id_str);
        }
        return msg;
    }

private:
    // 消息类型映射表
    static const std::map<MessageType, std::string>& getTypeToStringMap() {
        static const std::map<MessageType, std::string> map = {
            {MSG_LOGIN, "login"},
            {MSG_LOGOUT, "logout"},
            {MSG_CHAT, "message"},
            {MSG_LIST, "list"},
            {MSG_ACK, "ack"},
            {MSG_SYSTEM, "system"}
        };
        return map;
    }

    static const std::map<std::string, MessageType>& getStringToTypeMap() {
        static const std::map<std::string, MessageType> map = {
            {"login", MSG_LOGIN},
            {"logout", MSG_LOGOUT},
            {"message", MSG_CHAT},
            {"list", MSG_LIST},
            {"ack", MSG_ACK},
            {"system", MSG_SYSTEM}
        };
        return map;
    }

    // 获取消息类型字符串
    static std::string getTypeString(MessageType type) {
        auto& map = getTypeToStringMap();
        auto it = map.find(type);
        return (it != map.end()) ? it->second : "unknown";
    }

    // 从字符串获取消息类型
    static MessageType getTypeFromString(const std::string& type) {
        auto& map = getStringToTypeMap();
        auto it = map.find(type);
        return (it != map.end()) ? it->second : MSG_CHAT;
    }

    // 获取JSON字段值
    static std::string getValue(const std::string& json, const std::string& key) {
        std::string search = "\"" + key + "\":";
        size_t pos = json.find(search);
        if (pos == std::string::npos) {
            return "";
        }

        pos += search.length();
        // 跳过空白和引号
        while (pos < json.length() && (json[pos] == ' ' || json[pos] == '"')) {
            pos++;
        }

        // 提取字段值
        std::string result;
        bool in_escape = false;
        for (; pos < json.length(); pos++) {
            char ch = json[pos];
            if (in_escape) {
                result += ch;
                in_escape = false;
            } else if (ch == '\\') {
                in_escape = true;
            } else if (ch == '"' || ch == ',' || ch == '}') {
                break;
            } else {
                result += ch;
            }
        }
        return result;
    }

    // JSON转义
    static std::string escapeJson(const std::string& str) {
        static const std::map<char, std::string> escape_map = {
            {'"', "\\\""}, {'\\', "\\\\"}, {'\n', "\\n"},
            {'\r', "\\r"}, {'\t', "\\t"}
        };

        std::string result;
        result.reserve(str.length());
        for (char c : str) {
            auto it = escape_map.find(c);
            if (it != escape_map.end()) {
                result += it->second;
            } else {
                result += c;
            }
        }
        return result;
    }

    // JSON反转义
    static std::string unescapeJson(const std::string& str) {
        static const std::map<char, char> unescape_map = {
            {'"', '"'}, {'\\', '\\'}, {'n', '\n'},
            {'r', '\r'}, {'t', '\t'}
        };

        std::string result;
        result.reserve(str.length());
        bool escape = false;
        for (char c : str) {
            if (escape) {
                auto it = unescape_map.find(c);
                result += (it != unescape_map.end()) ? it->second : c;
                escape = false;
            } else if (c == '\\') {
                escape = true;
            } else {
                result += c;
            }
        }
        return result;
    }
};

#endif // PROTOCOL_H
