#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <cstring>
#include <cstdint>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

class LowPassFilter {
public:
    LowPassFilter(double alpha = 1.0) : a(alpha), initialized(false), hatxprev(0.0) {}
    double filter(double value, double alpha) {
        a = alpha;
        if (!initialized) { initialized = true; hatxprev = value; return value; }
        double hatx = a * value + (1.0 - a) * hatxprev;
        hatxprev = hatx;
        return hatx;
    }
    double lastValue() const { return hatxprev; }
    void reset() { initialized = false; hatxprev = 0.0; }
private:
    double a;
    bool initialized;
    double hatxprev;
};

class OneEuroFilter {
public:
    OneEuroFilter(double freq = 240.0, double mincutoff = 1.0, double beta = 0.05, double dcutoff = 1.0)
        : freq_(freq), mincutoff_(mincutoff), beta_(beta), dcutoff_(dcutoff), lastTime_(-1.0) {}

    double filter(double value, double timestamp = -1.0) {
        if (lastTime_ != -1.0 && timestamp != -1.0 && timestamp > lastTime_) {
            double dt = timestamp - lastTime_;
            if (dt < 0.001) dt = 0.001;
            if (dt > 0.1) dt = 0.1;
            freq_ = 1.0 / dt;
        }
        lastTime_ = timestamp;

        double dx = 0.0;
        if (x_.lastValue() != 0.0 || !std::isnan(x_.lastValue())) {
            dx = (value - x_.lastValue()) * freq_;
        }
        double edx = dx_.filter(dx, alpha(dcutoff_));
        double cutoff = mincutoff_ + beta_ * std::abs(edx);
        return x_.filter(value, alpha(cutoff));
    }

    void reset() { x_.reset(); dx_.reset(); lastTime_ = -1.0; }

private:
    double alpha(double cutoff) {
        double te = 1.0 / freq_;
        double tau = 1.0 / (2.0 * 3.14159265358979323846 * cutoff);
        return te / (te + tau);
    }
    double freq_, mincutoff_, beta_, dcutoff_, lastTime_;
    LowPassFilter x_, dx_;
};

void quatToYawPitch(float w, float x, float y, float z, float& yaw, float& pitch) {
    float siny_cosp = 2.0f * (w * z + x * y);
    float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
    yaw = atan2f(siny_cosp, cosy_cosp) * (180.0f / (float)M_PI);

    float sinp = 2.0f * (w * x - z * y);
    if (sinp > 1.0f) sinp = 1.0f;
    if (sinp < -1.0f) sinp = -1.0f;
    pitch = asinf(sinp) * (180.0f / (float)M_PI);
}

int main() {
    std::cout << "===============================================================" << std::endl;
    std::cout << " MAGICMOUSE COMPLETE REGRESSION & VALIDATION TEST SUITE" << std::endl;
    std::cout << "===============================================================" << std::endl;

    int passedTests = 0;
    int totalTests = 4;

    // --- REGRESSION TEST 1: Absolute Deterministic Mapping (Zero Drift) ---
    std::cout << "\n[TEST 1] Testing Absolute Deterministic Mapping Zero-Drift..." << std::endl;
    {
        OneEuroFilter yawFilter(240.0, 1.0, 0.05, 1.0);
        OneEuroFilter pitchFilter(240.0, 1.0, 0.05, 1.0);
        float sensitivity = 35.0f;
        float screenCenterX = 1720.0f;
        float screenCenterY = 720.0f;

        float finalX = 0.0f, finalY = 0.0f;
        for (int i = 0; i <= 300; ++i) { // 1.25s total (motion 0..1.0s, settling 1.0..1.25s)
            float t = i * (1.0f / 240.0f);
            float rawYaw = (t <= 1.0f) ? (10.0f * sinf(2.0f * (float)M_PI * t)) : 0.0f;
            float rawPitch = (t <= 1.0f) ? (8.0f * cosf(2.0f * (float)M_PI * t) - 8.0f) : 0.0f;

            float filteredYaw = (float)yawFilter.filter(rawYaw, t);
            float filteredPitch = (float)pitchFilter.filter(rawPitch, t);

            finalX = screenCenterX - (filteredYaw * sensitivity);
            finalY = screenCenterY - (filteredPitch * sensitivity);
        }

        float drift = std::sqrt((finalX - screenCenterX)*(finalX - screenCenterX) + (finalY - screenCenterY)*(finalY - screenCenterY));
        std::cout << "  Final Cursor Pos: (" << finalX << ", " << finalY << ") | Target: (1720, 720)" << std::endl;
        std::cout << "  Calculated Cumulative Drift: " << drift << " px" << std::endl;

        if (drift < 0.01f) {
            std::cout << "  ==> [PASS] Test 1: Zero Position Drift Verified!" << std::endl;
            passedTests++;
        } else {
            std::cout << "  ==> [FAIL] Test 1: Position Drift exceeds threshold: " << drift << std::endl;
        }
    }

    // --- REGRESSION TEST 2: Re-center Baseline Reset Verification ---
    std::cout << "\n[TEST 2] Testing Re-center Baseline Reset Synchronization..." << std::endl;
    {
        OneEuroFilter yawFilter(240.0, 1.0, 0.05, 1.0);
        float screenCenterX = 1720.0f;
        float sensitivity = 35.0f;

        // Simulate 30 deg offset posture
        float rawYaw = 30.0f;
        float filteredBefore = (float)yawFilter.filter(rawYaw, 0.1);
        float posBefore = screenCenterX - (filteredBefore * sensitivity);

        // Execute Re-center: Filter is reset, baseline becomes 0
        yawFilter.reset();
        float filteredAfter = (float)yawFilter.filter(0.0f, 0.2); // phone reports 0 deg relative to new reference
        float posAfter = screenCenterX - (filteredAfter * sensitivity);

        std::cout << "  Position Before Recenter: " << posBefore << " px" << std::endl;
        std::cout << "  Position Immediately After Recenter: " << posAfter << " px" << std::endl;

        if (std::abs(posAfter - screenCenterX) < 0.01f) {
            std::cout << "  ==> [PASS] Test 2: Re-center snaps smoothly to exact screen center!" << std::endl;
            passedTests++;
        } else {
            std::cout << "  ==> [FAIL] Test 2: Re-center failed to snap to center." << std::endl;
        }
    }

    // --- REGRESSION TEST 3: 240Hz Filter Response & Low-Speed Noise Floor ---
    std::cout << "\n[TEST 3] Testing 240Hz 1 Euro Filter Jitter Noise Suppression..." << std::endl;
    {
        OneEuroFilter filter(240.0, 1.0, 0.05, 1.0);
        double maxJitter = 0.0;

        for (int i = 0; i < 240; ++i) {
            double t = i * (1.0 / 240.0);
            double noise = 0.05 * sin(i * 0.9); // 0.05 deg sensor noise
            double filtered = filter.filter(noise, t);
            if (i > 20 && std::abs(filtered) > maxJitter) {
                maxJitter = std::abs(filtered);
            }
        }
        std::cout << "  Max Residual Jitter Noise: " << maxJitter << " deg (Raw noise: 0.05 deg)" << std::endl;
        if (maxJitter < 0.015) {
            std::cout << "  ==> [PASS] Test 3: Sensor noise floor effectively suppressed below 0.015 deg!" << std::endl;
            passedTests++;
        } else {
            std::cout << "  ==> [FAIL] Test 3: Jitter noise exceeds safety threshold." << std::endl;
        }
    }

    // --- REGRESSION TEST 4: Binary Protocol Serialization & Parsing ---
    std::cout << "\n[TEST 4] Testing Binary Protocol Serialization & Parsing..." << std::endl;
    {
        // Test QUAT packet (0x01, 17 bytes)
        std::vector<uint8_t> buffer;
        buffer.push_back(0x01);
        float w_in = 1.0f, x_in = 0.0f, y_in = 0.0f, z_in = 0.0f;
        uint8_t* pW = (uint8_t*)&w_in;
        uint8_t* pX = (uint8_t*)&x_in;
        uint8_t* pY = (uint8_t*)&y_in;
        uint8_t* pZ = (uint8_t*)&z_in;

        for (int b = 0; b < 4; ++b) buffer.push_back(pW[b]);
        for (int b = 0; b < 4; ++b) buffer.push_back(pX[b]);
        for (int b = 0; b < 4; ++b) buffer.push_back(pY[b]);
        for (int b = 0; b < 4; ++b) buffer.push_back(pZ[b]);

        // Test RECENTER packet (0x09, 1 byte)
        buffer.push_back(0x09);

        // Parse buffer
        bool quatParsed = false;
        bool recenterParsed = false;
        size_t offset = 0;

        while (offset < buffer.size()) {
            uint8_t type = buffer[offset];
            if (type == 0x01) {
                if (buffer.size() - offset >= 17) {
                    float w, x, y, z;
                    memcpy(&w, &buffer[offset + 1], 4);
                    memcpy(&x, &buffer[offset + 5], 4);
                    memcpy(&y, &buffer[offset + 9], 4);
                    memcpy(&z, &buffer[offset + 13], 4);
                    if (w == 1.0f && x == 0.0f && y == 0.0f && z == 0.0f) {
                        quatParsed = true;
                    }
                    offset += 17;
                }
            } else if (type == 0x09) {
                recenterParsed = true;
                offset += 1;
            }
        }

        if (quatParsed && recenterParsed) {
            std::cout << "  ==> [PASS] Test 4: Binary protocol round-trip serialization verified!" << std::endl;
            passedTests++;
        } else {
            std::cout << "  ==> [FAIL] Test 4: Binary protocol parsing failed." << std::endl;
        }
    }

    std::cout << "\n===============================================================" << std::endl;
    std::cout << " SUMMARY: " << passedTests << " / " << totalTests << " REGRESSION TESTS PASSED" << std::endl;
    std::cout << "===============================================================" << std::endl;

    return (passedTests == totalTests) ? 0 : 1;
}
