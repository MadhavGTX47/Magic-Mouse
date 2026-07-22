# MagicMouse

An Android air mouse for Windows that actually feels good to use. 

Most air mouse apps suffer from terrible cursor drift because they only use raw gyroscope data. MagicMouse combines 9-axis sensor fusion (gyro, accelerometer, magnetometer) on the phone with a C++ 1 Euro Filter on the PC to keep the cursor smooth and stable.

## How it Works (Under the Hood)
- **Android App:** Reads `TYPE_ROTATION_VECTOR` at maximum hardware frequency (matching high refresh displays dynamically) for absolute 3D orientation. Tapping "Re-center" synchronizes baselines on both phone and PC server, snapping the cursor smoothly to screen center.
- **Networking:** Streams data over a dedicated Bluetooth RFCOMM connection using a 17-byte **Compact Binary Protocol**, eliminating Wi-Fi signal congestion and router bufferbloat while keeping latency sub-5ms.
- **Windows Server:** A lightweight C++ background app using Winsock `AF_BTH`. It queries display hardware settings on-the-fly (`EnumDisplaySettings`) to dynamically match high-refresh rate monitors (e.g. **240 Hz**), filtering orientation deltas without frame-mismatch stutter.

---

> [!TIP]
> **Bluetooth Mode (Zero Network Jitter)**: MagicMouse on this branch operates completely over Bluetooth. Pair your phone with your PC in Windows Settings first, then launch the app to select your PC from the paired devices list.

---

## Features
- **Dynamic High Refresh Rate (240Hz+):** Automatically detects your monitor's display frequency (e.g., 240 Hz) and tunes the 1 Euro Filter and Android sensor pipeline for butter-smooth high-Hz gaming and desktop responsiveness.
- **Instant Server-Synchronized Re-center:** Tapping "Re-center" instantly resets filter baselines on both phone and C++ server, smoothly centering the pointer without position jumps.
- **Relative Delta Velocity Motion:** Works like an optical desktop mouse—rest your wrist anywhere comfortably. Move the cursor smoothly without arm fatigue or position snapping.
- **Sub-Pixel Fractional Remainder Accumulation:** Prevents truncation jitter on micro-movements, ensuring butter-smooth high-Hz tracking.
- **Compact Binary Protocol:** Ultra-lean 17-byte telemetry packets for minimum latency over Bluetooth RFCOMM.
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
