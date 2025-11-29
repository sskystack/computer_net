#include "rdt_socket.h"
#include "common.h"
#include <iostream>
#include <fstream>
#include <cstring>
#include <iomanip>

// 打印使用说明
void PrintUsage(const char* program_name) {
    std::cout << "Usage: " << program_name << " <remote_ip> <remote_port> <input_file> [window_size]" << std::endl;
    std::cout << "  remote_ip: Remote server IP address" << std::endl;
    std::cout << "  remote_port: Remote server port number" << std::endl;
    std::cout << "  input_file: File to send" << std::endl;
    std::cout << "  window_size: Receive window size (optional, default: 32)" << std::endl;
    std::cout << std::endl;
    std::cout << "Example: " << program_name << " 127.0.0.1 8888 test.txt 32" << std::endl;
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
    if (argc < 4) {
        PrintUsage(argv[0]);
        return 1;
    }

    std::string remote_ip = argv[1];
    uint16_t remote_port = (uint16_t)std::atoi(argv[2]);
    std::string input_file = argv[3];
    uint16_t window_size = (argc > 4) ? (uint16_t)std::atoi(argv[4]) : DEFAULT_WINDOW_SIZE;

    std::cout << "=== RDT Protocol - Sender Program ===" << std::endl;
    std::cout << "Remote: " << remote_ip << ":" << remote_port << std::endl;
    std::cout << "Input File: " << input_file << std::endl;
    std::cout << "Window Size: " << window_size << std::endl;
    std::cout << std::endl;

    // 打开文件
    std::ifstream file(input_file, std::ios::binary);
    if (!file.is_open()) {
        std::cerr << "✗ Error: Cannot open file: " << input_file << std::endl;
        return 1;
    }

    // 获取文件大小
    file.seekg(0, std::ios::end);
    uint64_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    std::cout << "File Size: " << FormatSize(file_size) << " (" << file_size << " bytes)" << std::endl;

    // 创建并初始化RDT套接字
    RDTSocket socket;
    if (!socket.Initialize(window_size)) {
        std::cerr << "✗ Error: Failed to initialize socket" << std::endl;
        file.close();
        return 1;
    }

    // 连接到服务器
    std::cout << "Connecting to server..." << std::endl;
    if (!socket.Connect(remote_ip, remote_port)) {
        std::cerr << "✗ Error: Connection failed" << std::endl;
        file.close();
        return 1;
    }
    std::cout << "✓ Connected to server" << std::endl << std::endl;

    // 发送文件
    std::cout << "Sending file..." << std::endl;
    Timer send_timer;

    uint8_t buffer[16384];  // 16KB读取缓冲区
    uint64_t total_sent = 0;
    int progress_step = file_size / 100;  // 每1%的进度显示一次
    if (progress_step == 0) progress_step = 1;

    while (file.good() && total_sent < file_size) {
        // 从文件读取数据
        file.read((char*)buffer, sizeof(buffer));
        int read_size = file.gcount();

        if (read_size > 0) {
            // 发送数据
            int sent = socket.Send(buffer, read_size);
            if (sent < 0) {
                std::cerr << "✗ Error: Send failed" << std::endl;
                break;
            }

            total_sent += sent;

            // 显示进度
            if (total_sent % progress_step == 0 || total_sent == file_size) {
                int progress = (int)((total_sent * 100) / file_size);
                std::cout << "\rProgress: " << std::setw(3) << progress << "% ("
                          << FormatSize(total_sent) << " / " << FormatSize(file_size) << ")";
                std::cout.flush();
            }
        }
    }
    std::cout << std::endl << std::endl;

    // 关闭连接
    std::cout << "Closing connection..." << std::endl;
    socket.Close();

    file.close();

    // 获取统计信息
    double elapsed_sec = send_timer.ElapsedSec();
    auto stats = socket.GetStatistics();

    // 输出统计信息
    std::cout << std::endl;
    std::cout << "=== Transfer Statistics ===" << std::endl;
    std::cout << "Total Time: " << std::fixed << std::setprecision(3) << elapsed_sec << " seconds" << std::endl;
    std::cout << "Data Sent: " << FormatSize(stats.bytes_sent) << " (" << stats.bytes_sent << " bytes)" << std::endl;
    std::cout << "Packets Sent: " << stats.packets_sent << std::endl;
    std::cout << "Packets Retransmitted: " << stats.packets_retransmitted << std::endl;
    std::cout << "Packets Dropped: " << stats.packets_dropped << std::endl;

    if (elapsed_sec > 0) {
        double throughput = stats.bytes_sent / elapsed_sec;
        double throughput_mbps = throughput / (1024 * 1024);
        std::cout << "Average Throughput: " << FormatSize((uint64_t)throughput) << "/s";
        std::cout << " (" << std::fixed << std::setprecision(2) << throughput_mbps << " Mbps)" << std::endl;
    }

    std::cout << std::endl;
    std::cout << "✓ File transmission completed" << std::endl;

    return 0;
}
