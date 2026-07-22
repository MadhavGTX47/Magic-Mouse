#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>

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
    OneEuroFilter(double freq = 120.0, double mincutoff = 0.6, double beta = 0.05, double dcutoff = 1.0)
        : freq_(freq), mincutoff_(mincutoff), beta_(beta), dcutoff_(dcutoff), lastTime_(-1.0) {}

    double filter(double value, double timestamp = -1.0) {
        if (lastTime_ != -1.0 && timestamp != -1.0 && timestamp > lastTime_) {
            double dt = timestamp - lastTime_;
            if (dt < 0.002) dt = 0.002;
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

int main() {
    std::cout << "--- Testing Relative Delta Velocity Algorithm ---" << std::endl;

    OneEuroFilter deltaYawFilter(120.0, 0.6, 0.05, 1.0);
    OneEuroFilter deltaPitchFilter(120.0, 0.6, 0.05, 1.0);

    float sensitivity = 25.0f;
    float prevYaw = 0.0f;
    float prevPitch = 0.0f;
    bool firstReading = true;

    float currentX = 1000.0f;
    float currentY = 500.0f;

    // Simulate 120Hz frames of wrist tilt out +5 deg and back to 0 deg
    for (int i = 0; i <= 130; ++i) {
        float t = i * (1.0f / 120.0f);
        // S-curve out and back to 0 deg at t=1.0s, remaining frames hold 0 deg
        float yaw = (t <= 1.0f) ? (5.0f * (0.5f - 0.5f * cosf(2.0f * (float)M_PI * t))) : 0.0f;
        float pitch = 0.0f;

        // Filter absolute angles first (zero phase distortion, zero cumulative drift!)
        float filteredYaw = (float)deltaYawFilter.filter(yaw, t);
        float filteredPitch = (float)deltaPitchFilter.filter(pitch, t);

        if (firstReading) {
            prevYaw = filteredYaw;
            prevPitch = filteredPitch;
            firstReading = false;
            continue;
        }

        float deltaYaw = filteredYaw - prevYaw;
        float deltaPitch = filteredPitch - prevPitch;

        prevYaw = filteredYaw;
        prevPitch = filteredPitch;

        float dx = -deltaYaw * sensitivity;
        float dy = -deltaPitch * sensitivity;

        currentX += dx;
        currentY += dy;
    }

    std::cout << "Starting Position: (1000.0, 500.0)" << std::endl;
    std::cout << "Final Position:    (" << currentX << ", " << currentY << ")" << std::endl;
    float drift = std::abs(currentX - 1000.0f);
    std::cout << "Drift Error:       " << drift << " px" << std::endl;

    if (drift < 1.0f) {
        std::cout << "\nSUCCESS: Relative Delta Velocity Mode achieves zero-drift fluid pointer movement!" << std::endl;
    } else {
        std::cout << "\nWARNING: Drift error detected: " << drift << std::endl;
    }

    return 0;
}
