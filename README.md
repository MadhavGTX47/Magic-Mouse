# MagicMouse

An Android air mouse for Windows that actually feels good to use. 

Most air mouse apps suffer from terrible cursor drift because they only use raw gyroscope data. MagicMouse combines 9-axis sensor fusion (gyro, accelerometer, magnetometer) on the phone with a C++ 1 Euro Filter on the PC to keep the cursor smooth and stable.

## How it Works (Under the Hood)
- **Android App:** Reads `TYPE_ROTATION_VECTOR` for absolute 3D orientation. Tapping "Re-center" offsets the orientation locally, so the phone does all the hard math. 
- **Networking:** Streams data at 120Hz over a dedicated Bluetooth RFCOMM connection using a 17-byte **Compact Binary Protocol**, eliminating Wi-Fi signal congestion and router bufferbloat while keeping latency sub-5ms.
- **Windows Server:** A lightweight C++ background app using Winsock `AF_BTH` socket server. It decodes binary quaternion packets, passes them through a 120Hz-tuned 1 Euro Filter, applies a continuous Cubic Hermite soft noise gate, and updates pointer coordinates using sub-pixel 16-bit `SendInput` precision.

---

> [!TIP]
> **Bluetooth Mode (Zero Network Jitter)**: MagicMouse on this branch operates completely over Bluetooth. Pair your phone with your PC in Windows Settings first, then launch the app to select your PC from the paired devices list.

---

## Features
- **Absolute Pointer Mapping:** Maps phone tilt directly to screen coordinates (like a Wii remote).
- **Sub-Pixel Ultra-Precision:** Uses continuous Cubic Hermite noise-gating, 1:1 linear micro-movement response, and 16-bit floating-point SendInput mapping to feel as precise and smooth as a high-DPI desktop gaming mouse.
- **Compact Binary Protocol:** Ultra-lean 17-byte 120Hz telemetry packets for minimum latency over Bluetooth RFCOMM.
- **Core Controls:** Left/Right/Middle clicks, double-tap, and a dedicated scroll strip.
- **Extras:** Physical volume button controls, customizable sensitivity, built-in shortcuts (Esc, Ctrl+C/V, Alt+Tab), and speech-to-text dictation.

## Getting Started

### 1. Build & Run the Windows Server
Requires `g++` (via MinGW / MSYS2). Open your terminal in the `server` directory and run:

```cmd
g++ src/main.cpp src/InputController.cpp -o MagicMouseServer.exe -lws2_32 -luser32
./MagicMouseServer.exe
```
This will start the Bluetooth RFCOMM server and listen for connections.

### 2. Build & Install the Android App
Requires Android SDK. Open your terminal in the `android_app` directory and run:

```cmd
./gradlew assembleDebug
```
Install the generated APK located at `app/build/outputs/apk/debug/app-debug.apk` to your phone, select your paired PC, and connect!
