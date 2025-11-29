#include "rdt_socket.h"
#include "common.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <iomanip>

// 打印使用说明
void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <listen_port> <output_file> [window_size]" << std::endl;
    std::cout << "  listen_port: Port to listen on" << std::endl;
    std::cout << "  output_file: File to save received data" << std::endl;
    std::cout << "  window_size: Receive window size (optional, default: 32)" << std::endl;
    std::cout << std::endl;
    std::cout << "Example: " << program_name << " 8888 received.txt 32" << std::endl;
}

// 格式化显示大小
std::string FormatSize(uint64_t bytes) {
    if (bytes < 1024) return std::to_string(bytes) + " B";
    if (bytes < 1024 * 1024) return std::to_string(bytes / 1024) + " KB";
    if (bytes < 1024 * 1024 * 1024) return std::to_string(bytes / (1024 * 1024)) + " MB";
    return std::to_string(bytes / (1024 * 1024 * 1024)) + " GB";
}

int main(int argc, char* argv[]) {
    // 检查命令行参数
    if (argc < 3) {
        PrintUsage(argv[0]);
        return 1;
    }

    uint16_t listen_port = (uint16_t)std::atoi(argv[1]);
    std::string output_file = argv[2];
    uint16_t window_size = (argc > 3) ? (uint16_t)std::atoi(argv[3]) : DEFAULT_WINDOW_SIZE;

    std::cout << "=== RDT Protocol - Receiver Program ===" << std::endl;
    std::cout << "Listen Port: " << listen_port << std::endl;
    std::cout << "Output File: " << output_file << std::endl;
    std::cout << "Window Size: " << window_size << std::endl;
    std::cout << std::endl;

    // 创建并初始化RDT套接字
    RDTSocket socket;
    if (!socket.Initialize(window_size)) {
        std::cerr << "✗ Error: Failed to initialize socket" << std::endl;
        return 1;
    }

    // 绑定到端口
    std::cerr << "[DEBUG] Before Bind() call" << std::endl;

    std::cout << "Binding to port " << listen_port << "..." << std::endl;
    std::cerr << "[DEBUG] About to call Bind()" << std::endl;

    bool bind_result = socket.Bind(listen_port);
    std::cerr << "[DEBUG] Bind() returned: " << (bind_result ? "true" : "false") << std::endl;

    if (!bind_result) {
        std::cerr << "✗ Error: Failed to bind to port" << std::endl;
        return 1;
    }

    std::cerr << "[DEBUG] After Bind() success check" << std::endl;
    std::cout << "✓ Bound to port " << listen_port << std::endl;
    std::cerr << "[DEBUG] About to call Listen()" << std::endl;

    // 开始监听
    std::cout << "Listening for connections..." << std::endl;
    if (!socket.Listen()) {
        std::cerr << "✗ Error: Failed to listen" << std::endl;
        return 1;
    }

    std::cerr << "[DEBUG] After Listen()" << std::endl;

    // 接受连接
    std::string remote_addr;
    uint16_t remote_port;
    std::cerr << "[DEBUG] Before Accept()" << std::endl;
    std::cout << "Waiting for connection..." << std::endl;
    if (!socket.Accept(remote_addr, remote_port)) {
        std::cerr << "✗ Error: Failed to accept connection" << std::endl;
        return 1;
    }

    std::cerr << "[DEBUG] After Accept()" << std::endl;
    std::cout << "✓ Connection accepted from " << remote_addr << ":" << remote_port << std::endl << std::endl;
    std::cerr << "[DEBUG] About to open file" << std::endl;
    std::ofstream output(output_file, std::ios::binary);
    if (!output.is_open()) {
        std::cerr << "✗ Error: Cannot open output file: " << output_file << std::endl;
        return 1;
    }

    // 接收文件
    std::cout << "Receiving file..." << std::endl;
    Timer recv_timer;

    uint8_t buffer[65536];  // 64KB接收缓冲区
    uint64_t total_received = 0;
    int display_counter = 0;

    while (true) {
        // 设置接收超时（5秒）
        socket.SetRecvTimeout(5000);

        // 接收数据
        int received = socket.Recv(buffer, sizeof(buffer));

        if (received == 0) {
            // 连接关闭
            std::cout << "\nConnection closed by remote host" << std::endl;
            break;
        } else if (received < 0) {
            std::cerr << "\n✗ Error: Receive failed" << std::endl;
            break;
        }

        // 写入文件
        output.write((const char*)buffer, received);
        total_received += received;

        // 每5MB显示一次进度
        display_counter++;
        if (display_counter >= 5 || received == 0) {
            std::cout << "\rReceived: " << FormatSize(total_received) << " (" << total_received << " bytes)";
            std::cout.flush();
            display_counter = 0;
        }
    }
    std::cout << std::endl << std::endl;

    // 关闭文件
    output.close();

    // 关闭连接
    std::cout << "Closing connection..." << std::endl;
    socket.Close();

    // 获取统计信息
    double elapsed_sec = recv_timer.ElapsedSec();
    auto stats = socket.GetStatistics();

    // 输出统计信息
    std::cout << std::endl;
    std::cout << "=== Transfer Statistics ===" << std::endl;
    std::cout << "Total Time: " << std::fixed << std::setprecision(3) << elapsed_sec << " seconds" << std::endl;
    std::cout << "Data Received: " << FormatSize(stats.bytes_received) << " (" << stats.bytes_received << " bytes)" << std::endl;
    std::cout << "Packets Received: " << stats.packets_received << std::endl;
    std::cout << "Packets Dropped: " << stats.packets_dropped << std::endl;

    if (elapsed_sec > 0) {
        double throughput = stats.bytes_received / elapsed_sec;
        double throughput_mbps = throughput / (1024 * 1024);
        std::cout << "Average Throughput: " << FormatSize((uint64_t)throughput) << "/s";
        std::cout << " (" << std::fixed << std::setprecision(2) << throughput_mbps << " Mbps)" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "✓ File transmission completed" << std::endl;
    std::cout << "✓ File saved to: " << output_file << std::endl;

    return 0;
}
