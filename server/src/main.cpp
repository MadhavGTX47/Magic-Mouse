#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2bth.h>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <cstdint>
#include "InputController.h"

// Link with ws2_32.lib
#pragma comment(lib, "ws2_32.lib")

// Custom MagicMouse Bluetooth UUID: 94f39d29-7d6d-437d-973b-fba39e49d4ee
static const GUID MagicMouse_UUID = { 0x94f39d29, 0x7d6d, 0x437d, { 0x97, 0x3b, 0xfb, 0xa3, 0x9e, 0x49, 0xd4, 0xee } };

int main() {
    // Set DPI awareness so GetSystemMetrics returns physical pixels instead of scaled coordinates
    SetProcessDPIAware();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed." << std::endl;
        return 1;
    }

    SOCKET listenSocket = socket(AF_BTH, SOCK_STREAM, BTHPROTO_RFCOMM);
    if (listenSocket == INVALID_SOCKET) {
        std::cerr << "Bluetooth socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    SOCKADDR_BTH sab;
    memset(&sab, 0, sizeof(sab));
    sab.addressFamily = AF_BTH;
    sab.btAddr = 0; // BTH_ADDR_NULL (local radio)
    sab.serviceClassId = MagicMouse_UUID;
    sab.port = BT_PORT_ANY;

    if (bind(listenSocket, (SOCKADDR*)&sab, sizeof(sab)) == SOCKET_ERROR) {
        std::cerr << "Bluetooth bind failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    // Get assigned RFCOMM channel
    int sabLen = sizeof(sab);
    getsockname(listenSocket, (SOCKADDR*)&sab, &sabLen);

    // Register SDP service record so Android apps can discover the service by UUID
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

    if (WSASetService(&saSet, RNRSERVICE_REGISTER, 0) == SOCKET_ERROR) {
        std::cout << "[INFO] WSASetService SDP registration note: " << WSAGetLastError() 
                  << " (Direct pair connection ready)" << std::endl;
    }

    if (listen(listenSocket, 1) == SOCKET_ERROR) {
        std::cerr << "Bluetooth listen failed with error: " << WSAGetLastError() << std::endl;
        closesocket(listenSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "\n=========================================\n";
    std::cout << " MagicMouse Bluetooth Server Listening!\n";
    std::cout << " Waiting for Bluetooth connection...\n";
    std::cout << "=========================================\n\n";

    InputController inputController;

    while (true) {
        SOCKADDR_BTH clientAddr;
        int clientAddrLen = sizeof(clientAddr);
        SOCKET clientSocket = accept(listenSocket, (SOCKADDR*)&clientAddr, &clientAddrLen);
        if (clientSocket == INVALID_SOCKET) {
            std::cerr << "Accept failed with error: " << WSAGetLastError() << std::endl;
            continue;
        }

        std::cout << "[INFO] Phone Connected via Bluetooth RFCOMM! Listening for telemetry..." << std::endl;

        std::vector<uint8_t> buffer;
        char recvChunk[1024];

        while (true) {
            int bytesRead = recv(clientSocket, recvChunk, sizeof(recvChunk), 0);
            if (bytesRead <= 0) {
                std::cout << "[INFO] Phone Disconnected. Waiting for reconnection..." << std::endl;
                break;
            }

            buffer.insert(buffer.end(), recvChunk, recvChunk + bytesRead);

            // Parse binary packets from stream buffer
            size_t offset = 0;
            struct QuatFrame { float w, x, y, z; bool valid = false; } latestQuat;

            while (offset < buffer.size()) {
                uint8_t packetType = buffer[offset];

                if (packetType == 0x01) { // QUAT: 17 bytes (1 type + 4 floats)
                    if (buffer.size() - offset < 17) break; // Wait for complete binary frame
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
                    inputController.clickMouse(btn, action);
                    offset += 3;
                } else if (packetType == 0x03) { // SCROLL: 3 bytes
                    if (buffer.size() - offset < 3) break;
                    int16_t delta;
                    memcpy(&delta, &buffer[offset + 1], 2);
                    inputController.scroll(delta);
                    offset += 3;
                } else if (packetType == 0x04) { // SENS: 5 bytes
                    if (buffer.size() - offset < 5) break;
                    float sens;
                    memcpy(&sens, &buffer[offset + 1], 4);
                    inputController.setSensitivity(sens);
                    offset += 5;
                } else if (packetType == 0x05) { // DOUBLECLICK: 1 byte
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
                    if (!action.empty()) inputController.executeShortcut(action);
                    offset += 2;
                } else if (packetType == 0x07) { // VOL: 2 bytes
                    if (buffer.size() - offset < 2) break;
                    int8_t dir = (int8_t)buffer[offset + 1];
                    inputController.adjustVolume(dir > 0 ? "UP" : "DOWN");
                    offset += 2;
                } else if (packetType == 0x08) { // DICT: 3 bytes + N text bytes
                    if (buffer.size() - offset < 3) break;
                    uint16_t textLen;
                    memcpy(&textLen, &buffer[offset + 1], 2);
                    if (buffer.size() - offset < 3 + textLen) break;
                    std::string text((char*)&buffer[offset + 3], textLen);
                    inputController.typeText(text);
                    offset += 3 + textLen;
                } else if (packetType == 0x09) { // RECENTER: 1 byte
                    inputController.recenter();
                    offset += 1;
                } else if (packetType == 0xFF) { // PING: 1 byte
                    offset += 1;
                } else {
                    // Unknown byte, skip 1 byte to resync
                    offset += 1;
                }
            }

            // Erase processed bytes from buffer
            buffer.erase(buffer.begin(), buffer.begin() + offset);

            // Execute the latest quaternion movement (smart conflation drops obsolete frames)
            if (latestQuat.valid) {
                auto now = std::chrono::high_resolution_clock::now();
                double timestamp = std::chrono::duration_cast<std::chrono::duration<double>>(
                    now.time_since_epoch()
                ).count();
                inputController.processQuaternion(latestQuat.w, latestQuat.x, latestQuat.y, latestQuat.z, timestamp);
            }
        }

        closesocket(clientSocket);
    }

    closesocket(listenSocket);
    WSACleanup();
    return 0;
}
