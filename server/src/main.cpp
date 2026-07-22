#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <ws2bth.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include <thread>
#include <mutex>
#include "InputController.h"

// Link with ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

// Custom MagicMouse Bluetooth UUID: 94f39d29-7d6d-437d-973b-fba39e49d4ee
static const GUID MagicMouse_UUID = { 0x94f39d29, 0x7d6d, 0x437d, { 0x97, 0x3b, 0xfb, 0xa3, 0x9e, 0x49, 0xd4, 0xee } };

std::mutex controllerMutex;

void parseBinaryBuffer(std::vector<uint8_t>& buffer, InputController& inputController) {
    size_t offset = 0;
    struct QuatFrame { float w, x, y, z; bool valid = false; } latestQuat;

    while (offset < buffer.size()) {
        uint8_t packetType = buffer[offset];

        if (packetType == 0x01) { // QUAT: 17 bytes (1 type + 4 floats)
            if (buffer.size() - offset < 17) break;
            memcpy(&latestQuat.w, &buffer[offset + 1], 4);
            memcpy(&latestQuat.x, &buffer[offset + 5], 4);
            memcpy(&latestQuat.y, &buffer[offset + 9], 4);
            memcpy(&latestQuat.z, &buffer[offset + 13], 4);
            latestQuat.valid = true;
            offset += 17;
        } else if (packetType == 0x02) { // CLICK: 3 bytes
            if (buffer.size() - offset < 3) break;
            uint8_t btnId = buffer[offset + 1];
            uint8_t actionId = buffer[offset + 2];
            char btn = (btnId == 1) ? 'L' : ((btnId == 2) ? 'R' : 'M');
            std::string action = (actionId == 1) ? "DOWN" : "UP";
            std::lock_guard<std::mutex> lock(controllerMutex);
            inputController.clickMouse(btn, action);
            offset += 3;
        } else if (packetType == 0x03) { // SCROLL: 3 bytes
            if (buffer.size() - offset < 3) break;
            int16_t delta;
            memcpy(&delta, &buffer[offset + 1], 2);
            std::lock_guard<std::mutex> lock(controllerMutex);
            inputController.scroll(delta);
            offset += 3;
        } else if (packetType == 0x04) { // SENS: 5 bytes
            if (buffer.size() - offset < 5) break;
            float sens;
            memcpy(&sens, &buffer[offset + 1], 4);
            std::lock_guard<std::mutex> lock(controllerMutex);
            inputController.setSensitivity(sens);
            offset += 5;
        } else if (packetType == 0x05) { // DOUBLECLICK: 1 byte
            std::lock_guard<std::mutex> lock(controllerMutex);
            inputController.doubleClick();
            offset += 1;
        } else if (packetType == 0x06) { // SHORTCUT: 2 bytes
            if (buffer.size() - offset < 2) break;
            uint8_t scId = buffer[offset + 1];
            std::string action = "";
            if (scId == 1) action = "ESC";
            else if (scId == 2) action = "CTRL_C";
            else if (scId == 3) action = "CTRL_V";
            else if (scId == 4) action = "CTRL_Z";
            else if (scId == 5) action = "ALT_TAB";
            if (!action.empty()) {
                std::lock_guard<std::mutex> lock(controllerMutex);
                inputController.executeShortcut(action);
            }
            offset += 2;
        } else if (packetType == 0x07) { // VOL: 2 bytes
            if (buffer.size() - offset < 2) break;
            int8_t dir = (int8_t)buffer[offset + 1];
            std::lock_guard<std::mutex> lock(controllerMutex);
            inputController.adjustVolume(dir > 0 ? "UP" : "DOWN");
            offset += 2;
        } else if (packetType == 0x08) { // DICT: 3 bytes + N text bytes
            if (buffer.size() - offset < 3) break;
            uint16_t textLen;
            memcpy(&textLen, &buffer[offset + 1], 2);
            if (buffer.size() - offset < 3 + textLen) break;
            std::string text((char*)&buffer[offset + 3], textLen);
            std::lock_guard<std::mutex> lock(controllerMutex);
            inputController.typeText(text);
            offset += 3 + textLen;
        } else if (packetType == 0x09) { // RECENTER: 1 byte
            std::lock_guard<std::mutex> lock(controllerMutex);
            inputController.recenter();
            offset += 1;
        } else if (packetType == 0xFF) { // PING: 1 byte
            offset += 1;
        } else {
            offset += 1;
        }
    }

    if (offset > 0) {
        buffer.erase(buffer.begin(), buffer.begin() + offset);
    }

    if (latestQuat.valid) {
        auto now = std::chrono::high_resolution_clock::now();
        double timestamp = std::chrono::duration_cast<std::chrono::duration<double>>(
            now.time_since_epoch()
        ).count();
        std::lock_guard<std::mutex> lock(controllerMutex);
        inputController.processQuaternion(latestQuat.w, latestQuat.x, latestQuat.y, latestQuat.z, timestamp);
    }
}

void runBluetoothServer(InputController* inputController) {
    SOCKET listenSocket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "[Bluetooth] Socket creation failed." << std::endl;
        return;
    }

    SOCKADDR_BTH sab;
    memset(&sab, 0, sizeof(sab));
    sab.addressFamily = AF_BTH;
    sab.btAddr = 0;
    sab.serviceClassId = MagicMouse_UUID;
    sab.port = BT_PORT_ANY;

    if (bind(listenSocket, (SOCKADDR*)&sab, sizeof(sab)) == SOCKET_ERROR) {
        std::cerr << "[Bluetooth] Bind failed." << std::endl;
        closesocket(listenSocket);
        return;
    }

    int sabLen = sizeof(sab);
    getsockname(listenSocket, (SOCKADDR*)&sab, &sabLen);

    WSAQUERYSET saSet;
    memset(&saSet, 0, sizeof(saSet));
    saSet.dwSize = sizeof(saSet);
    saSet.lpszServiceInstanceName = (LPSTR)"MagicMouse Server";
    saSet.lpServiceClassId = (LPGUID)&MagicMouse_UUID;
    saSet.dwNameSpace = NS_BTH;
    saSet.dwNumberOfCsAddrs = 1;

    CSADDR_INFO csAddr;
    memset(&csAddr, 0, sizeof(csAddr));
    csAddr.LocalAddr.lpSockaddr = (LPSOCKADDR)&sab;
    csAddr.LocalAddr.iSockaddrLength = sizeof(sab);
    csAddr.iSocketType = SOCK_STREAM;
    csAddr.iProtocol = BTHPROTO_RFCOMM;

    saSet.lpcsaBuffer = &csAddr;
    WSASetService(&saSet, RNRSERVICE_REGISTER, 0);

    if (listen(listenSocket, 1) == SOCKET_ERROR) {
        std::cerr << "[Bluetooth] Listen failed." << std::endl;
        closesocket(listenSocket);
        return;
    }

    std::cout << "[Bluetooth] Server listening for RFCOMM connections..." << std::endl;

    while (true) {
        SOCKADDR_BTH clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket, (SOCKADDR*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) continue;

        std::cout << "[INFO] Phone Connected via Bluetooth RFCOMM!" << std::endl;

        std::vector<uint8_t> buffer;
        char recvChunk[1024];

        while (true) {
            int bytesRead = recv(clientSocket, recvChunk, sizeof(recvChunk), 0);
            if (bytesRead <= 0) {
                std::cout << "[INFO] Bluetooth disconnected." << std::endl;
                break;
            }

            buffer.insert(buffer.end(), recvChunk, recvChunk + bytesRead);
            parseBinaryBuffer(buffer, *inputController);
        }

        closesocket(clientSocket);
    }
    closesocket(listenSocket);
}

void runWifiServer(InputController* inputController, int port) {
    SOCKET udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (udpSocket == INVALID_SOCKET) {
        std::cerr << "[Wi-Fi] UDP Socket creation failed." << std::endl;
        return;
    }

    sockaddr_in serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = INADDR_ANY;

    if (bind(udpSocket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        std::cerr << "[Wi-Fi] UDP Bind failed on port " << port << std::endl;
        closesocket(udpSocket);
        return;
    }

    std::cout << "[Wi-Fi] UDP Server listening on port " << port << "..." << std::endl;

    std::vector<uint8_t> streamBuffer;
    char buffer[2048];
    sockaddr_in clientAddr;
    int clientAddrLen = sizeof(clientAddr);

    while (true) {
        int bytesRead = recvfrom(udpSocket, buffer, sizeof(buffer) - 1, 0, (sockaddr*)&clientAddr, &clientAddrLen);
        if (bytesRead <= 0) continue;

        // Check if string PING
        if (bytesRead >= 4 && strncmp(buffer, "PING", 4) == 0) {
            sendto(udpSocket, "PONG", 4, 0, (sockaddr*)&clientAddr, clientAddrLen);
            continue;
        }

        streamBuffer.insert(streamBuffer.end(), buffer, buffer + bytesRead);
        parseBinaryBuffer(streamBuffer, *inputController);
    }

    closesocket(udpSocket);
}

int main() {
    SetProcessDPIAware();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    std::cout << "\n===================================================\n";
    std::cout << " MagicMouse Dual Server (Bluetooth + Wi-Fi UDP)\n";
    std::cout << "===================================================\n\n";

    InputController inputController;

    std::thread btThread(runBluetoothServer, &inputController);
    std::thread wifiThread(runWifiServer, &inputController, 9876);

    btThread.join();
    wifiThread.join();

    WSACleanup();
    return 0;
}
