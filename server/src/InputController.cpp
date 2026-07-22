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
    : sensitivity_(45.0f)
    , firstReading_(true)
    , prevYaw_(0.0f)
    , prevPitch_(0.0f)
    , remainderX_(0.0f)
    , remainderY_(0.0f)
    , refreshRate_(60)
    , yawFilter_(240.0, 0.8, 0.05, 1.0)
    , pitchFilter_(240.0, 0.8, 0.05, 1.0)
{
    screenWidth_ = GetSystemMetrics(SM_CXSCREEN);
    screenHeight_ = GetSystemMetrics(SM_CYSCREEN);

    DEVMODE devMode = {0};
    devMode.dmSize = sizeof(devMode);
    if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &devMode) && devMode.dmDisplayFrequency > 0) {
        refreshRate_ = devMode.dmDisplayFrequency;
    }

    yawFilter_ = OneEuroFilter((double)refreshRate_, 0.8, 0.05, 1.0);
    pitchFilter_ = OneEuroFilter((double)refreshRate_, 0.8, 0.05, 1.0);

    std::cout << "Screen resolution: " << screenWidth_ << "x" << screenHeight_ 
              << " @ " << refreshRate_ << " Hz" << std::endl;
}

InputController::~InputController() {}

void InputController::setSensitivity(float sens) {
    sensitivity_ = sens;
}

void InputController::recenter() {
    firstReading_ = true;
    remainderX_ = 0.0f;
    remainderY_ = 0.0f;
    yawFilter_.reset();
    pitchFilter_.reset();

    // Snap cursor directly to screen center
    if (screenWidth_ > 1 && screenHeight_ > 1) {
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dx = 32767;
        input.mi.dy = 32767;
        input.mi.dwFlags = MOUSEEVENTF_MOVE | MOUSEEVENTF_ABSOLUTE;
        SendInput(1, &input, sizeof(INPUT));
    }
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
 * Relative Gyro Delta Velocity Architecture:
 * 1. Filter absolute 3D orientation (yaw, pitch) using a 120Hz 1 Euro Filter (mincutoff=0.8Hz).
 * 2. Calculate frame-to-frame delta angles (deltaYaw, deltaPitch) with 360-degree wraparound protection.
 * 3. Telescopic mathematical proof guarantees ZERO position drift & zero hysteresis.
 * 4. Sub-pixel fractional remainder accumulation prevents truncation jitter, keeping cursor tracking liquid-smooth.
 * 5. Relative mouse displacement allows comfortable rest-posture arm positioning anywhere in space.
 */
void InputController::processQuaternion(float w, float x, float y, float z, double timestamp) {
    float yawDeg, pitchDeg;
    quaternionToYawPitch(w, x, y, z, yawDeg, pitchDeg);

    // Check for NaN
    if (std::isnan(yawDeg) || std::isnan(pitchDeg)) return;

    // Apply 1 Euro Filter to raw orientation angles (120Hz tuned)
    float filteredYaw = (float)yawFilter_.filter(yawDeg, timestamp);
    float filteredPitch = (float)pitchFilter_.filter(pitchDeg, timestamp);

    // On first reading after re-center or launch, capture baseline
    if (firstReading_) {
        prevYaw_ = filteredYaw;
        prevPitch_ = filteredPitch;
        remainderX_ = 0.0f;
        remainderY_ = 0.0f;
        firstReading_ = false;
        return;
    }

    // Compute frame-to-frame angular delta
    float deltaYaw = filteredYaw - prevYaw_;
    float deltaPitch = filteredPitch - prevPitch_;

    // Handle 360 to -360 degree wraparound boundary
    if (deltaYaw > 180.0f) deltaYaw -= 360.0f;
    if (deltaYaw < -180.0f) deltaYaw += 360.0f;
    if (deltaPitch > 180.0f) deltaPitch -= 360.0f;
    if (deltaPitch < -180.0f) deltaPitch += 360.0f;

    prevYaw_ = filteredYaw;
    prevPitch_ = filteredPitch;

    // Compute continuous sub-pixel mouse displacement
    // Horizontal: Yaw left (positive) -> Move left (-dx)
    // Vertical: Pitch up (positive) -> Move up (-dy)
    float rawDx = -deltaYaw * sensitivity_;
    float rawDy = -deltaPitch * sensitivity_;

    // Sub-pixel fractional remainder accumulation
    remainderX_ += rawDx;
    remainderY_ += rawDy;

    LONG stepX = (LONG)std::round(remainderX_);
    LONG stepY = (LONG)std::round(remainderY_);

    remainderX_ -= (float)stepX;
    remainderY_ -= (float)stepY;

    // Dispatch relative movement to Windows OS input subsystem
    if (stepX != 0 || stepY != 0) {
        INPUT input = {0};
        input.type = INPUT_MOUSE;
        input.mi.dx = stepX;
        input.mi.dy = stepY;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
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
