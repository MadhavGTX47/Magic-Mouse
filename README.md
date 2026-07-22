# MagicMouse

An Android air mouse for Windows that actually feels good to use. 

Most air mouse apps suffer from terrible cursor drift because they only use raw gyroscope data. MagicMouse combines 9-axis sensor fusion (gyro, accelerometer, magnetometer) on the phone with a C++ 1 Euro Filter on the PC to keep the cursor smooth and stable.

## How it Works (Under the Hood)
- **Android App:** Reads `TYPE_ROTATION_VECTOR` for absolute 3D orientation. Tapping "Re-center" offsets the orientation locally, so the phone does all the hard math. 
- **Networking:** Streams data at 120Hz over UDP. To prevent lag spikes, the app holds a high-performance Wi-Fi lock and drops outdated movements (using a conflated channel) while preserving clicks.
- **Windows Server:** A lightweight C++ background app. It uses `ioctlsocket(FIONREAD)` to quickly drain network buffers, processes the movements through a 1 Euro Filter, and moves the pointer via Windows `SendInput`.

---

> [!IMPORTANT]
> **For Best Performance:** Because MagicMouse uses UDP for near-instant cursor response, network congestion on home routers can cause jitter or lag spikes. For the smoothest experience, connect your phone directly to your PC's Mobile Hotspot.

---

## Features
- **Absolute Pointer Mapping:** Maps phone tilt directly to screen coordinates (like a Wii remote).
- **Core Controls:** Left/Right/Middle clicks, double-tap, and a dedicated scroll strip.
- **Touchpad Mode:** Relative tracking if you want to use your screen like a trackpad.
- **Extras:** Physical volume button controls, customizable sensitivity, built-in shortcuts (Esc, Ctrl+C/V, Alt+Tab), and speech-to-text dictation.

## Getting Started

### 1. Build & Run the Windows Server
Requires `g++` (via MSYS2 or MinGW). Open your terminal in the `server` directory and run:

```cmd
g++ src/main.cpp src/InputController.cpp -o MagicMouseServer.exe -lws2_32 -luser32
./MagicMouseServer.exe
```
This will start the server and print your local IP addresses.

### 2. Build & Install the Android App
Requires Android SDK. Open your terminal in the `android_app` directory and run:

```cmd
./gradlew assembleDebug
```
Install the generated APK located at `app/build/outputs/apk/debug/app-debug.apk` to your phone, enter your PC's IP address, and connect.
