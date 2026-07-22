#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>

int main() {
    DEVMODE devMode = {0};
    devMode.dmSize = sizeof(devMode);
    if (EnumDisplaySettings(NULL, ENUM_CURRENT_SETTINGS, &devMode)) {
        std::cout << "Detected Monitor Resolution: " << devMode.dmPelsWidth << "x" << devMode.dmPelsHeight << std::endl;
        std::cout << "Detected Monitor Refresh Rate: " << devMode.dmDisplayFrequency << " Hz" << std::endl;
    } else {
        std::cout << "EnumDisplaySettings failed. Defaulting to 60 Hz." << std::endl;
    }
    return 0;
}
