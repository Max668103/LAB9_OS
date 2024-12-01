#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <sstream>
#include <winsock2.h>  // For socket programming

#pragma comment(lib, "ws2_32.lib")  // Link with ws2_32.lib for Windows sockets

void ReaderThread(SOCKET sock) {
    char buffer[512];
    std::string fullMessage;
    while (true) {
        int bytesReceived = recv(sock, buffer, sizeof(buffer), 0);
        if (bytesReceived > 0) {
            fullMessage.append(buffer, bytesReceived);
            if (fullMessage.find("\n") != std::string::npos) { // Detect end of message
                std::cout << "Received from server: " << fullMessage << std::endl;
                fullMessage.clear(); // Reset for next message
            }
        }
        else if (bytesReceived == 0) {
            std::cout << "Server disconnected.\n";
            break;
        }
        else {
            std::cerr << "Recv failed with error: " << WSAGetLastError() << "\n";
            break;
        }
    }
}

// ------------------- Main function -------------------
int main() {
    WSADATA wsaData;
    SOCKET sock;
    sockaddr_in serverAddr;

    // Initialize Winsock
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed. Error: " << WSAGetLastError() << "\n";
        return 1;
    }

    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        std::cerr << "Socket creation failed. Error: " << WSAGetLastError() << "\n";
        WSACleanup();
        return 1;
    }

    // Set up server address (replace with your server's IP and port)
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345); // Port number
    serverAddr.sin_addr.s_addr = inet_addr("127.0.0.1"); // Server IP

    // Connect to server
    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "Connection to server failed. Error: " << WSAGetLastError() << "\n";
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    std::cout << "Connected to server.\n";
    std::cout << "Available subscription types: [weather], [currency], [stock].\n";
    std::cout << "Enter subscription type: ";

    // Input subscription types
    std::string command;
    std::cin >> command;

    std::istringstream ss(command);
    std::string subscriptionType;
    std::vector<std::string> subscriptions;

    // Parse subscription types
    while (std::getline(ss, subscriptionType, ',')) {
        subscriptions.push_back(subscriptionType);
    }

    // Send subscription request to server
    for (const auto& type : subscriptions) {
        std::string subscriptionCommand = "subscribe:" + type;
        int bytesSent = send(sock, subscriptionCommand.c_str(), subscriptionCommand.size(), 0);

        if (bytesSent == SOCKET_ERROR) {
            std::cerr << "Failed to send subscription request for " << type << ". Error: " << WSAGetLastError() << "\n";
            closesocket(sock);
            WSACleanup();
            return 1;
        }
    }

    std::cout << "Subscription requests sent. Waiting for messages...\n";
    // Start thread to read from server
    std::thread readerThread(ReaderThread, sock);
    readerThread.join();

    // Clean up
    readerThread.detach();
    closesocket(sock);
    WSACleanup();
    return 0;
}
