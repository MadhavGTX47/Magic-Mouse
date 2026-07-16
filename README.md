# MagicMouse

MagicMouse turns your Android phone into a drift-free air mouse for Windows.

I built this because existing apps are either full of ads or suffer from terrible gyroscope drift. Most projects just use raw gyro data, which means your cursor slowly slides off the screen even when your hand is still. 

MagicMouse fixes this by using 9-axis sensor fusion (combining the phone's gyro, accelerometer, and magnetometer) to figure out its absolute orientation. We then pass that data through a 1 Euro Filter on the C++ server. The result is a **flawlessly smooth, zero-jitter pointer with ultra-low latency**. Because it streams directly over a 120Hz UDP connection, the mouse movement is instantaneous and feels just like a physical hardware mouse.

## Features

* **Drift-Free Pointing**: Uses Android's hardware-fused `TYPE_ROTATION_VECTOR` to send absolute orientation instead of raw gyroscope deltas.
* **Low Latency**: Communicates over a local Wi-Fi network using UDP packets.
* **Absolute Coordinate Mapping**: Maps phone yaw and pitch directly to screen pixels. Re-center the cursor at any time to establish a new reference point.
* **Cursor Control Features**: Left click, right click, middle click, double tap, and scroll wheel support.
* **Touchpad Mode**: Switch to relative tracking if you want to set the phone on a desk and swipe instead of pointing.
* **Dictation**: Tap the microphone icon to convert speech to text and send it directly to your PC.
* **Keyboard Shortcuts**: Built-in buttons for common shortcuts like Esc, Ctrl+C, Ctrl+V, Alt+Tab, and Ctrl+Z.
* **Volume Control**: Use your phone's physical volume buttons to adjust PC volume.
## Network Recommendation (Important)

MagicMouse streams orientation data over UDP at 120Hz to achieve amazing latency and flawlessly smooth cursor movement, even on high refresh rate displays. 

Because we use UDP to achieve this speed, standard Wi-Fi routers can sometimes cause packet loss or buffering if there is interference or network congestion. To completely eliminate lag spikes and freezes, **it is highly recommended to connect your phone directly to your PC's Mobile Hotspot** (or connect your PC to your phone's hotspot). This creates a direct point-to-point connection that bypasses your router completely.

## Architecture

The system consists of two parts: a native Android app and a C++ Windows server.

### Android App (Client)
Written in Kotlin, built with standard Android views (no heavy game engines). 
* **Sensor Collection**: It registers a `SensorEventListener` for `TYPE_ROTATION_VECTOR`. This provides a quaternion `[w, x, y, z]` representing the phone's absolute orientation in 3D space relative to the Earth.
* **Re-centering Math**: When the user taps the Re-center button, the app stores the current orientation as a reference quaternion. On subsequent sensor events, it calculates the relative difference: `Q_relative = Q_reference_inverse * Q_current`. This relative quaternion is sent over UDP, keeping the phone's workload minimal.
* **Network Stability**: To combat UDP packet bursts during Wi-Fi lag spikes, the app holds a High-Performance Wi-Fi WakeLock to prevent the OS from sleeping the radio. It uses a dual-channel coroutine architecture: a `CONFLATED` channel physically drops obsolete mouse movements during network stalls, while an `UNLIMITED` channel ensures clicks and shortcuts are safely queued and never lost.
* **UI**: Uses Android standard layouts with haptic feedback, custom buttons, and Android's native SpeechRecognizer.

### Windows Desktop (Server)
A lightweight C++ executable running in the background.
* **UDP Socket & Smart Drain**: Listens for incoming UDP packets on port 9876. To prevent Windows `SendInput` from freezing the OS when processing a massive burst of delayed packets, the server uses `ioctlsocket(FIONREAD)` to instantly drain the entire OS packet queue. It processes only the latest mouse movement and discards obsolete ones, while safely executing all batched clicks.
* **Math and Filtering**: Extracts yaw and pitch angles from the incoming quaternions. It applies a 1 Euro Filter (exponential smoothing with speed-based jitter reduction) for a completely stable pointer. An acceleration curve scales the input so fast swipes cover more screen area while slow movements allow precise pixel targeting.
* **Input Simulation**: Uses the Windows `SendInput` API with `MOUSEEVENTF_ABSOLUTE` for hardware-level cursor positioning. It also simulates keyboard strokes for shortcuts and volume control.
* **DPI Awareness**: Calls `SetProcessDPIAware()` on startup so it can detect your physical screen resolution accurately, avoiding limits caused by Windows display scaling.

## How to Run

### 1. Windows Server
1. Ensure you have the `g++` compiler installed (via MSYS2 or MinGW).
2. Open a terminal in the `server` directory.
3. Run the following command to build the server:
   ```cmd
   g++ src/main.cpp src/InputController.cpp -o MagicMouseServer.exe -lws2_32 -luser32 -mwindows
   ```
4. Run the executable `MagicMouseServer.exe`. It will print a list of local IP addresses. Note the IP address.

### 2. Android App
1. Ensure you have the Android SDK and Java installed.
2. Build the APK using Gradle:
   ```cmd
   cd android_app
   gradlew assembleDebug
   ```
3. Install the generated APK (`android_app/app/build/outputs/apk/debug/app-debug.apk`) on your Android phone.
4. Open the app, enter the IP address printed by the Windows server, and connect.

## Contributing

We welcome contributions. If you want to add new features or fix bugs, follow these steps:
1. Fork the repository.
2. Create a new branch for your feature.
3. Make your changes in the Android code (`android_app/`) or Windows C++ code (`server/`).
4. Ensure both the server and Android app compile and run correctly.
5. Submit a pull request.
