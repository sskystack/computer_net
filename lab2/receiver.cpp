#include "rdt_socket.h"
#include <iostream>
#include <cstring>
#include <winsock2.h>

#pragma comment(lib, "ws2_32.lib")

void printUsage(const char* prog_name) {
    printf("Usage: %s <local_port> <save_file_path>\n", prog_name);
    printf("Example: %s 5001 l2/received.jpg\n", prog_name);
}

int main(int argc, char* argv[]) {
    printf("[*] Starting receiver...\n");
    fflush(stdout);

    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0) {
        printf("[ERROR] WSAStartup failed\n");
        return 1;
    }

    if (argc != 3) {
        printf("[ERROR] Invalid parameters\n");
        printUsage(argv[0]);
        WSACleanup();
        return 1;
    }

    uint16_t local_port = atoi(argv[1]);
    const char* save_path = argv[2];

    printf("========================================\n");
    printf("  Reliable Data Transfer Protocol - Receiver\n");
    printf("========================================\n\n");

    RdtSocket receiver;

    if (!receiver.listen(local_port)) {
        printf("[ERROR] Failed to listen on port\n");
        WSACleanup();
        return 1;
    }

    RdtSocket* client = receiver.accept();
    if (!client) {
        printf("[ERROR] Failed to accept connection\n");
        receiver.close();
        WSACleanup();
        return 1;
    }

    if (!client->recvFile(save_path)) {
        printf("[ERROR] File reception failed\n");
        client->close();
        delete client;
        receiver.close();
        WSACleanup();
        return 1;
    }

    printf("\n========================================\n");
    printf("  Reception completed, program exiting\n");
    printf("========================================\n");

    client->close();
    delete client;
    receiver.close();

    WSACleanup();

    return 0;
}
