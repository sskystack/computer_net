#ifndef RDT_SOCKET_H
#define RDT_SOCKET_H

#include "protocol.h"
#include "packet.h"
#include "window_manager.h"
#include "congestion_control.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <chrono>

#ifdef _WIN32
    #include <winsock2.h>
    typedef int socklen_t;
#else
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #define INVALID_SOCKET -1
    #define SOCKET_ERROR -1
    typedef int SOCKET;
#endif

// ============================================
// 可靠数据传输套接字类
// ============================================

/**
 * RDT 套接字类
 * 基于 UDP 实现面向连接的可靠数据传输
 * 支持连接管理、流量控制、拥塞控制、错误检测等功能
 */
class RDTSocket {
public:
    /**
     * 构造函数
     */
    RDTSocket();

    /**
     * 析构函数
     */
    ~RDTSocket();

    /**
     * 初始化套接字
     * @param window_size 接收窗口大小（数据包数）
     * @return true表示成功
     */
    bool Initialize(uint16_t window_size = DEFAULT_WINDOW_SIZE);

    /**
     * 绑定到指定端口（作为服务器）
     * @param port 端口号
     * @return true表示成功
     */
    bool Bind(uint16_t port);

    /**
     * 监听连接请求（作为服务器）
     * @param backlog 待处理连接队列大小
     * @return true表示成功
     */
    bool Listen(int backlog = 1);

    /**
     * 接受连接请求（阻塞）
     * @param out_addr 输出：远端地址
     * @param out_port 输出：远端端口
     * @return true表示成功获得连接
     */
    bool Accept(std::string& out_addr, uint16_t& out_port);

    /**
     * 连接到远端（作为客户端）
     * @param remote_ip 远端IP地址
     * @param remote_port 远端端口号
     * @return true表示成功连接
     */
    bool Connect(const std::string& remote_ip, uint16_t remote_port);

    /**
     * 发送数据
     * @param data 数据指针
     * @param length 数据长度
     * @return 实际发送的字节数，-1表示失败
     */
    int Send(const uint8_t* data, int length);

    /**
     * 接收数据（阻塞）
     * @param buffer 数据缓冲区指针
     * @param length 缓冲区大小
     * @return 接收的字节数，0表示连接关闭，-1表示错误
     */
    int Recv(uint8_t* buffer, int length);

    /**
     * 关闭连接
     * @return true表示成功
     */
    bool Close();

    /**
     * 获取连接状态
     * @return 连接状态（参考protocol.h中的ConnectionState）
     */
    ConnectionState GetState() const;

    /**
     * 获取连接状态名称
     * @return 状态名称字符串
     */
    const char* GetStateName() const;

    /**
     * 获取统计信息
     */
    struct Statistics {
        uint64_t bytes_sent;           // 发送的总字节数
        uint64_t bytes_received;       // 接收的总字节数
        uint64_t packets_sent;         // 发送的总包数
        uint64_t packets_received;     // 接收的总包数
        uint64_t packets_retransmitted; // 重传的包数
        uint64_t packets_dropped;      // 丢弃的包数
        double average_throughput;     // 平均吞吐率（字节/秒）
    };

    /**
     * 获取统计信息
     * @return 统计信息结构体
     */
    Statistics GetStatistics() const;

    /**
     * 设置接收超时
     * @param timeout_ms 超时时间（毫秒）
     */
    void SetRecvTimeout(int timeout_ms);

private:
    // ============ 枚举和常量 ============
    static const int RECV_BUFFER_SIZE = 65536;  // 接收缓冲区大小
    static const int SEND_BUFFER_SIZE = 65536;  // 发送缓冲区大小

    // ============ 套接字和地址 ============
    SOCKET udp_socket_;                         // UDP套接字
    struct sockaddr_in local_addr_;             // 本地地址
    struct sockaddr_in remote_addr_;            // 远端地址
    bool is_server_;                            // 是否为服务器端

    // ============ 连接状态 ============
    ConnectionState state_;                     // 当前连接状态
    mutable std::mutex state_mutex_;            // 状态保护锁

    // ============ 序列号管理 ============
    uint32_t local_seq_;                        // 本地序列号
    uint32_t remote_seq_;                       // 远端序列号
    uint32_t expected_seq_;                     // 期望的下一个序列号

    // ============ 窗口管理 ============
    SendWindow send_window_;                    // 发送窗口
    ReceiveWindow recv_window_;                 // 接收窗口
    uint16_t remote_window_size_;               // 远端通告的窗口大小

    // ============ 拥塞控制 ============
    CongestionControl congestion_control_;      // 拥塞控制

    // ============ 数据缓冲 ============
    std::queue<uint8_t> recv_queue_;            // 接收数据队列
    std::mutex recv_queue_mutex_;               // 接收队列保护锁
    std::condition_variable recv_cv_;           // 接收数据条件变量

    // ============ 线程管理 ============
    std::thread recv_thread_;                   // 接收线程
    std::thread retransmit_thread_;             // 重传线程
    volatile bool running_;                     // 线程运行标志
    std::mutex running_mutex_;                  // 运行标志保护锁

    // ============ 计时相关 ============
    uint32_t rto_;                              // 重传超时时间（毫秒）
    uint32_t rto_min_;                          // 最小RTO
    uint32_t rto_max_;                          // 最大RTO
    long long last_ack_time_;                   // 上一次收到ACK的时间
    int recv_timeout_ms_;                       // 接收超时时间

    // ============ 统计信息 ============
    Statistics stats_;                          // 统计信息
    mutable std::mutex stats_mutex_;            // 统计信息保护锁
    long long start_time_;                      // 连接开始时间

    // ============ 私有方法 ============

    /**
     * 接收线程函数
     */
    void ReceiveThreadFunc();

    /**
     * 重传检测和处理线程函数
     */
    void RetransmitThreadFunc();

    /**
     * 处理接收到的数据包
     * @param packet 接收到的数据包
     * @param remote_addr 远端地址
     */
    void HandleReceivedPacket(const Packet& packet, const struct sockaddr_in& remote_addr);

    /**
     * 处理SYN包
     */
    void HandleSyn(const Packet& packet);

    /**
     * 处理SYN-ACK包
     */
    void HandleSynAck(const Packet& packet);

    /**
     * 处理ACK包
     */
    void HandleAck(const Packet& packet);

    /**
     * 处理数据包
     */
    void HandleData(const Packet& packet);

    /**
     * 处理FIN包
     */
    void HandleFin(const Packet& packet);

    /**
     * 发送数据包到远端
     * @param packet 数据包指针
     * @return 发送的字节数，-1表示失败
     */
    int SendPacket(const Packet* packet);

    /**
     * 重新发送等待中的数据包
     */
    void RetransmitWaitingPackets();

    /**
     * 更新RTO（往返时间）
     * @param sample_rtt 采样的RTT
     */
    void UpdateRTO(long long sample_rtt);

    /**
     * 进入指定状态
     * @param new_state 新的连接状态
     */
    void SetState(ConnectionState new_state);

    /**
     * 启动接收和重传线程
     */
    bool StartThreads();

    /**
     * 停止接收和重传线程
     */
    void StopThreads();

    /**
     * 清理资源
     */
    void Cleanup();

    /**
     * 关闭UDP套接字
     */
    void CloseSocket();
};

#endif // RDT_SOCKET_H
