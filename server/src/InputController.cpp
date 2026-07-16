#include "InputController.h"
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <cmath>
#include <vector>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

InputController::InputController()
    : sensitivity_(100.0f)
    , yawFilter_(60.0, 1.0, 0.05, 1.0)
    , pitchFilter_(60.0, 1.0, 0.05, 1.0)
    , firstReading_(true)
{
    // Cache screen resolution at startup
    screenWidth_ = GetSystemMetrics(SM_CXSCREEN);
    screenHeight_ = GetSystemMetrics(SM_CYSCREEN);
    screenCenterX_ = screenWidth_ / 2;
    screenCenterY_ = screenHeight_ / 2;
    std::cout << "Screen resolution: " << screenWidth_ << "x" << screenHeight_ << std::endl;
}

InputController::~InputController() {}

void InputController::setSensitivity(float sens) {
    sensitivity_ = sens;
}

/**
 * Convert a unit quaternion to yaw (horizontal) and pitch (vertical) Euler angles.
 * 
 * Quaternion format: [w, x, y, z]
 * 
 * We use the aerospace/intrinsic ZYX convention:
 *   Yaw   = rotation around Z axis (left/right pointing)
 *   Pitch = rotation around Y axis (up/down tilt)
 * 
 * The output is in degrees.
 */
void InputController::quaternionToYawPitch(float w, float x, float y, float z, float& yaw, float& pitch) {
    // Yaw (Z-axis rotation): atan2(2*(w*z + x*y), 1 - 2*(y*y + z*z))
    float siny_cosp = 2.0f * (w * z + x * y);
    float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
    yaw = atan2f(siny_cosp, cosy_cosp) * (180.0f / (float)M_PI);

    // Pitch (X-axis rotation): asin(2*(w*x - z*y))
    // Clamped to avoid NaN from floating point drift outside [-1, 1]
    float sinp = 2.0f * (w * x - z * y);
    if (sinp > 1.0f) sinp = 1.0f;
    if (sinp < -1.0f) sinp = -1.0f;
    pitch = asinf(sinp) * (180.0f / (float)M_PI);
}

/**
 * Process an incoming relative quaternion from the phone.
 * 
 * The phone has already computed Q_relative = Q_ref_inverse * Q_current,
 * so this quaternion represents the phone's rotation RELATIVE to where
 * the user was pointing when they last pressed "re-center".
 * 
 * We extract yaw and pitch, pass them through a 1 Euro filter to
 * eliminate micro-jitter dynamically, scale by sensitivity, and set the cursor position.
 */
void InputController::processQuaternion(float w, float x, float y, float z, double timestamp) {
    float yawDeg, pitchDeg;
    quaternionToYawPitch(w, x, y, z, yawDeg, pitchDeg);

    // Check for NaN which can cause a hardware exception on integer cast
    if (std::isnan(yawDeg) || std::isnan(pitchDeg)) return;

    // On first reading after a re-center, snap directly (no smoothing lag)
    if (firstReading_) {
        yawFilter_.reset();
        pitchFilter_.reset();
        firstReading_ = false;
    }

    // Apply 1 Euro Filter
    float filteredYaw = yawFilter_.filter(yawDeg, timestamp);
    float filteredPitch = pitchFilter_.filter(pitchDeg, timestamp);

    // Calculate delta from center (where user was pointing at re-center)
    float deltaYaw = filteredYaw;
    float deltaPitch = filteredPitch;

    // Dead zone filter (ignore micro-jitter)
    if (std::abs(deltaYaw) < 0.15f) deltaYaw = 0.0f;
    if (std::abs(deltaPitch) < 0.15f) deltaPitch = 0.0f;

    // Mouse Acceleration Curve (non-linear scaling)
    float accelYaw = deltaYaw * std::pow(std::abs(deltaYaw / 10.0f) + 1.0f, 0.5f);
    float accelPitch = deltaPitch * std::pow(std::abs(deltaPitch / 10.0f) + 1.0f, 0.5f);

    // X-Axis inverted (Left is left, Right is right)
    int cursorX = screenCenterX_ - (int)(accelYaw * sensitivity_);
    int cursorY = screenCenterY_ - (int)(accelPitch * sensitivity_);  // Inverted: pitch up = screen up = lower Y

    // Clamp to screen bounds
    if (cursorX < 0) cursorX = 0;
    if (cursorX >= screenWidth_) cursorX = screenWidth_ - 1;
    if (cursorY < 0) cursorY = 0;
    if (cursorY >= screenHeight_) cursorY = screenHeight_ - 1;

    // Use SendInput with absolute coordinates for pixel-perfect placement.
    // MOUSEEVENTF_ABSOLUTE uses a 0..65535 coordinate system mapped to the screen.
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    // Prevent divide by zero if screen bounds are invalid
    if (screenWidth_ > 1 && screenHeight_ > 1) {
        input.mi.dx = (LONG)((cursorX * 65535) / (screenWidth_ - 1));
        input.mi.dy = (LONG)((cursorY * 65535) / (screenHeight_ - 1));
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        SendInput(1, &input, sizeof(INPUT));
    }
}

void InputController::clickMouse(char button, const std::string& action) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;

    if (button == 'L') {
        input.mi.dwFlags = (action == "DOWN") ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
    } else if (button == 'R') {
        input.mi.dwFlags = (action == "DOWN") ? MOUSEEVENTF_RIGHTDOWN : MOUSEEVENTF_RIGHTUP;
    } else if (button == 'M') {
        input.mi.dwFlags = (action == "DOWN") ? MOUSEEVENTF_MIDDLEDOWN : MOUSEEVENTF_MIDDLEUP;
    } else {
        return;
    }

    SendInput(1, &input, sizeof(INPUT));
}

void InputController::adjustVolume(const std::string& direction) {
    WORD wVk = 0;
    if (direction == "UP") {
        wVk = VK_VOLUME_UP;
    } else if (direction == "DOWN") {
        wVk = VK_VOLUME_DOWN;
    } else {
        return;
    }

    INPUT inputs[2] = {};

    // Key Down
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = wVk;

    // Key Up
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = wVk;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;

    SendInput(2, inputs, sizeof(INPUT));
}

void InputController::typeText(const std::string& text) {
    if (text.empty()) return;

    // Convert UTF-8 to UTF-16 (std::wstring)
    int wlen = MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, NULL, 0);
    if (wlen <= 0) return;

    std::wstring wtext(wlen, 0);
    MultiByteToWideChar(CP_UTF8, 0, text.c_str(), -1, &wtext[0], wlen);
    wtext.resize(wlen - 1); // remove null terminator

    for (wchar_t wc : wtext) {
        INPUT inputs[2] = {};

        // Key down (Unicode scan code)
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = wc;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;

        // Key up
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = wc;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;

        SendInput(2, inputs, sizeof(INPUT));
    }
}

void InputController::doubleClick() {
    INPUT inputs[4] = {0};
    for (int i = 0; i < 4; ++i) inputs[i].type = INPUT_MOUSE;
    inputs[0].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[1].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    inputs[2].mi.dwFlags = MOUSEEVENTF_LEFTDOWN;
    inputs[3].mi.dwFlags = MOUSEEVENTF_LEFTUP;
    SendInput(4, inputs, sizeof(INPUT));
}

void InputController::scroll(int delta) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_WHEEL;
    // Standard mouse wheel delta is 120 per notch
    input.mi.mouseData = (DWORD)(delta * WHEEL_DELTA);
    SendInput(1, &input, sizeof(INPUT));
}

void InputController::moveTouchpad(float dx, float dy) {
    INPUT input = {0};
    input.type = INPUT_MOUSE;
    input.mi.dwFlags = MOUSEEVENTF_MOVE;
    // Scale touchpad movement appropriately (e.g. 2x multiplier for snappier feel)
    input.mi.dx = (LONG)(dx * 2.0f);
    input.mi.dy = (LONG)(dy * 2.0f);
    SendInput(1, &input, sizeof(INPUT));
}

void InputController::executeShortcut(const std::string& shortcut) {
    INPUT inputs[4] = {0};
    for (int i = 0; i < 4; ++i) inputs[i].type = INPUT_KEYBOARD;

    if (shortcut == "ESC") {
        inputs[0].ki.wVk = VK_ESCAPE;
        inputs[1].ki.wVk = VK_ESCAPE;
        inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
    } else if (shortcut == "CTRL_C" || shortcut == "CTRL_V" || shortcut == "CTRL_Z") {
        WORD vkChar = 0;
        if (shortcut == "CTRL_C") vkChar = 'C';
        else if (shortcut == "CTRL_V") vkChar = 'V';
        else if (shortcut == "CTRL_Z") vkChar = 'Z';

        inputs[0].ki.wVk = VK_CONTROL;
        inputs[1].ki.wVk = vkChar;
        inputs[2].ki.wVk = vkChar;
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].ki.wVk = VK_CONTROL;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));
    } else if (shortcut == "ALT_TAB") {
        inputs[0].ki.wVk = VK_MENU; // ALT
        inputs[1].ki.wVk = VK_TAB;
        inputs[2].ki.wVk = VK_TAB;
        inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
        inputs[3].ki.wVk = VK_MENU;
        inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
        SendInput(4, inputs, sizeof(INPUT));
    }
}
