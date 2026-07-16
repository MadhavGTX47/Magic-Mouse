#ifndef INPUT_CONTROLLER_H
#define INPUT_CONTROLLER_H

#include <string>

#include "OneEuroFilter.h"

class InputController {
public:
    InputController();
    ~InputController();

    // Process a quaternion orientation packet and move the cursor.
    // timestamp helps 1 Euro filter estimate dynamic frequency.
    void processQuaternion(float w, float x, float y, float z, double timestamp = -1.0);

    // Mouse clicking: 'L' or 'R', 'DOWN' or 'UP'
    void clickMouse(char button, const std::string& action);

    // Volume control: "UP" or "DOWN"
    void adjustVolume(const std::string& direction);

    // Dictation typing: types out the string
    void typeText(const std::string& text);

    // Set sensitivity multiplier (pixels per degree)
    void setSensitivity(float sens);

    // Double click the left mouse button
    void doubleClick();

    // Scroll the mouse wheel
    void scroll(int delta);

    // Move mouse relative (for touchpad mode)
    void moveTouchpad(float dx, float dy);

    // Execute a keyboard shortcut
    void executeShortcut(const std::string& shortcut);

private:
    float sensitivity_;     // Pixels per degree of rotation
    bool firstReading_;     // Whether this is the first quaternion after reset
    int screenWidth_;       // Cached screen resolution
    int screenHeight_;
    int screenCenterX_;
    int screenCenterY_;

    OneEuroFilter yawFilter_;
    OneEuroFilter pitchFilter_;

    // Extract yaw and pitch (in degrees) from a quaternion
    void quaternionToYawPitch(float w, float x, float y, float z, float& yaw, float& pitch);
};

#endif // INPUT_CONTROLLER_H
