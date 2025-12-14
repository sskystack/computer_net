#include "rdt_socket.h"
#include <iostream>
#include <cstring>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

void printUsage(const char* prog_name) {
    printf("Usage: %s <file_path> <receiver_ip> <receiver_port>\n", prog_name);
    printf("Example: %s l2/testfile/helloworld.txt 127.0.0.1 5001\n", prog_name);
}

int main(int argc, char* argv[]) {
    printf("[*] Starting sender...\n");
    fflush(stdout);

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("[ERROR] WSAStartup failed\n");
        return 1;
    }

    if (argc != 4) {
        printf("[ERROR] Invalid parameters\n");
        printUsage(argv[0]);
        WSACleanup();
        return 1;
    }

    const char* file_path = argv[1];
    const char* remote_ip = argv[2];
    uint16_t remote_port = atoi(argv[3]);

    printf("========================================\n");
    printf("  Reliable Data Transfer Protocol - Sender\n");
    printf("========================================\n\n");

    RdtSocket sender;

    if (!sender.bind("127.0.0.1", 0)) {
        printf("[ERROR] Failed to bind local address\n");
        WSACleanup();
        return 1;
    }

    if (!sender.connect(remote_ip, remote_port)) {
        printf("[ERROR] Failed to connect to receiver\n");
        sender.close();
        WSACleanup();
        return 1;
    }

    if (!sender.sendFile(file_path)) {
        printf("[ERROR] File transfer failed\n");
        sender.close();
        WSACleanup();
        return 1;
    }

    printf("\n========================================\n");
    printf("  Transfer completed, program exiting\n");
    printf("========================================\n");

    sender.close();
    WSACleanup();

    return 0;
}
