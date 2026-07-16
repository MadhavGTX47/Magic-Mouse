#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "InputController.h"

// Link with ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_PORT 9876
#define BUFFER_BUFLEN 1024

void printLocalIPs() {
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) == 0) {
        struct hostent* host = gethostbyname(hostname);
        if (host != nullptr) {
            std::cout << "\n=========================================\n";
            std::cout << "MagicMouse Server - Available IP Addresses\n";
            std::cout << "=========================================\n";
            for (int i = 0; host->h_addr_list[i] != nullptr; ++i) {
                struct in_addr addr;
                memcpy(&addr, host->h_addr_list[i], sizeof(struct in_addr));
                std::cout << " -> " << inet_ntoa(addr) << std::endl;
            }
            std::cout << "=========================================\n\n";
        }
    } else {
        std::cerr << "Failed to get local hostname." << std::endl;
    }
}

int main() {
    // Set DPI awareness so GetSystemMetrics returns physical pixels instead of scaled coordinates
    SetProcessDPIAware();

    WSADATA wsaData;
    int iResult;

    // Initialize Winsock
    iResult = WSAStartup(MAKEWORD(2, 2), &wsaData);
    if (iResult != 0) {
        std::cerr << "WSAStartup failed with error: " << iResult << std::endl;
        return 1;
    }

    // Print IPs so the user knows what to type in the Android app
    printLocalIPs();

    // Create a UDP Socket
    SOCKET serverSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (serverSocket == INVALID_SOCKET) {
        std::cerr << "socket failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // Set 500ms receive timeout so recvfrom doesn't block forever when disconnected
    DWORD timeout = 500;
    setsockopt(serverSocket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    // Bind the socket to the port
    sockaddr_in serverService;
    serverService.sin_family = AF_INET;
    serverService.sin_addr.s_addr = INADDR_ANY;
    serverService.sin_port = htons(DEFAULT_PORT);

    iResult = bind(serverSocket, (SOCKADDR*)&serverService, sizeof(serverService));
    if (iResult == SOCKET_ERROR) {
        std::cerr << "bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Server listening on UDP port " << DEFAULT_PORT << "...\n" << std::endl;

    InputController inputController;
    char recvbuf[BUFFER_BUFLEN + 1];
    sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);

    auto lastRecvTime = std::chrono::high_resolution_clock::now();
    bool isConnected = false;

    std::cout << "Waiting for connection from Android app..." << std::endl;

    while (true) {
        // Receive packet (blocking)
        int bytesReceived = recvfrom(serverSocket, recvbuf, BUFFER_BUFLEN, 0, (SOCKADDR*)&clientAddr, &clientAddrLen);
        auto now = std::chrono::high_resolution_clock::now();
        double dtMs = std::chrono::duration<double, std::milli>(now - lastRecvTime).count();
        lastRecvTime = now;

        if (bytesReceived > 0) {
            if (!isConnected) {
                std::cout << "[INFO] Client Connected! Receiving data..." << std::endl;
                isConnected = true;
            }
        } else if (dtMs > 4000.0 && isConnected) {
            std::cout << "[INFO] Client Disconnected. Waiting for reconnection..." << std::endl;
            isConnected = false;
        }

        if (bytesReceived == SOCKET_ERROR) {
            int err = WSAGetLastError();
            // WSAETIMEDOUT happens every 500ms if no data arrives. WSAEINTR is normal interruption.
            if (err != WSAEINTR && err != WSAETIMEDOUT) {
                std::cerr << "recvfrom failed with error: " << err << std::endl;
            }
            continue; // Skip processing
        }

        // Protect against buffer overflow
        if (bytesReceived < 0) bytesReceived = 0;
        if (bytesReceived > BUFFER_BUFLEN) bytesReceived = BUFFER_BUFLEN;

        // Null-terminate the string
        recvbuf[bytesReceived] = '\0';
        std::string firstPacket(recvbuf, bytesReceived);

        std::vector<std::string> packetsToProcess;
        std::string latestQuat = "";

        if (firstPacket.rfind("QUAT:", 0) == 0) {
            latestQuat = firstPacket;
        } else {
            packetsToProcess.push_back(firstPacket);
        }

        // Smart Drain: Pull all pending packets from the OS buffer without blocking
        u_long bytesAvailable = 0;
        ioctlsocket(serverSocket, FIONREAD, &bytesAvailable);
        while (bytesAvailable > 0) {
            int br = recvfrom(serverSocket, recvbuf, BUFFER_BUFLEN, 0, (SOCKADDR*)&clientAddr, &clientAddrLen);
            if (br > 0) {
                recvbuf[br] = '\0';
                std::string p(recvbuf, br);
                if (p.rfind("QUAT:", 0) == 0) {
                    latestQuat = p; // Overwrite older QUAT (drops obsolete mouse movements!)
                } else {
                    packetsToProcess.push_back(p);
                }
            }
            ioctlsocket(serverSocket, FIONREAD, &bytesAvailable);
        }

        // Process the most recent QUAT packet first
        if (!latestQuat.empty()) {
            packetsToProcess.insert(packetsToProcess.begin(), latestQuat);
        }

        // Process Packet Batch
        for (const std::string& packet : packetsToProcess) {
        if (packet == "PING") {
            // Reply with PONG
            sendto(serverSocket, "PONG", 4, 0, (SOCKADDR*)&clientAddr, clientAddrLen);
        } else if (packet.rfind("QUAT:", 0) == 0) {
            // Format: QUAT:w:x:y:z
            // Relative quaternion from the phone (already has re-center applied)
            try {
                size_t p1 = 4;  // after "QUAT"
                size_t p2 = packet.find(':', p1 + 1);
                size_t p3 = packet.find(':', p2 + 1);
                size_t p4 = packet.find(':', p3 + 1);
                if (p2 != std::string::npos && p3 != std::string::npos && p4 != std::string::npos) {
                    float w = std::stof(packet.substr(p1 + 1, p2 - p1 - 1));
                    float x = std::stof(packet.substr(p2 + 1, p3 - p2 - 1));
                    float y = std::stof(packet.substr(p3 + 1, p4 - p3 - 1));
                    float z = std::stof(packet.substr(p4 + 1));
                    
                    double timestamp = std::chrono::duration_cast<std::chrono::duration<double>>(
                        now.time_since_epoch()
                    ).count();
                    
                    auto beforeInput = std::chrono::high_resolution_clock::now();
                    inputController.processQuaternion(w, x, y, z, timestamp);
                    auto afterInput = std::chrono::high_resolution_clock::now();
                    double inputDtMs = std::chrono::duration<double, std::milli>(afterInput - beforeInput).count();
                    if (inputDtMs > 5.0) {
                        std::cout << "[WARNING] SendInput OS block! Took " << inputDtMs << " ms." << std::endl;
                    }
                }
            } catch (const std::exception& e) {
                // Ignore malformed packets
            }
        } else if (packet.rfind("CLICK:", 0) == 0) {
            // Format: CLICK:btn:action
            size_t firstColon = 5;
            size_t secondColon = packet.find(':', firstColon + 1);
            if (secondColon != std::string::npos) {
                char btn = packet[firstColon + 1];
                std::string action = packet.substr(secondColon + 1);
                inputController.clickMouse(btn, action);
            }
        } else if (packet.rfind("SENS:", 0) == 0) {
            try {
                float sens = std::stof(packet.substr(5));
                inputController.setSensitivity(sens);
            } catch (const std::exception& e) {}
        } else if (packet.rfind("DOUBLECLICK:L", 0) == 0) {
            inputController.doubleClick();
        } else if (packet.rfind("SCROLL:", 0) == 0) {
            // Format: SCROLL:delta
            try {
                int delta = std::stoi(packet.substr(7));
                inputController.scroll(delta);
            } catch (const std::exception& e) {}
        } else if (packet.rfind("TOUCHPAD:", 0) == 0) {
            // Format: TOUCHPAD:dx:dy
            try {
                size_t p1 = 8;
                size_t p2 = packet.find(':', p1 + 1);
                if (p2 != std::string::npos) {
                    float dx = std::stof(packet.substr(p1 + 1, p2 - p1 - 1));
                    float dy = std::stof(packet.substr(p2 + 1));
                    inputController.moveTouchpad(dx, dy);
                }
            } catch (const std::exception& e) {}
        } else if (packet.rfind("SHORTCUT:", 0) == 0) {
            // Format: SHORTCUT:type
            std::string type = packet.substr(9);
            inputController.executeShortcut(type);
        } else if (packet.rfind("VOL:", 0) == 0) {
            // Format: VOL:dir
            std::string dir = packet.substr(4);
            inputController.adjustVolume(dir);
        } else if (packet.rfind("DICT:", 0) == 0) {
            // Format: DICT:text
            // Format: DICT:text
            std::string text = packet.substr(5);
            inputController.typeText(text);
        }
        } // End of packet processing loop
    }

    // Clean up
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}
