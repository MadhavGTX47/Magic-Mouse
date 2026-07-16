#ifndef ONE_EURO_FILTER_H
#define ONE_EURO_FILTER_H

#include <cmath>

class LowPassFilter {
public:
    LowPassFilter(double alpha = 1.0) : a(alpha), initialized(false), hatxprev(0.0) {}

    double filter(double value, double alpha) {
        a = alpha;
        if (!initialized) {
            initialized = true;
            hatxprev = value;
            return value;
        }
        double hatx = a * value + (1.0 - a) * hatxprev;
        hatxprev = hatx;
        return hatx;
    }

    double lastValue() const { return hatxprev; }
    void reset() { initialized = false; }

private:
    double a;
    bool initialized;
    double hatxprev;
};

class OneEuroFilter {
public:
    OneEuroFilter(double freq = 60.0, double mincutoff = 1.0, double beta = 0.0, double dcutoff = 1.0)
        : freq_(freq), mincutoff_(mincutoff), beta_(beta), dcutoff_(dcutoff) {
        lastTime_ = -1.0;
    }

    double filter(double value, double timestamp = -1.0) {
        // Estimate frequency based on timestamps if provided
        if (lastTime_ != -1.0 && timestamp != -1.0 && timestamp > lastTime_) {
            double dt = timestamp - lastTime_;
            // Clamp dt to prevent network jitter from creating insanely high frequencies
            // which causes alpha to drop to 0 and freeze the cursor.
            if (dt < 0.005) dt = 0.005; // Max 200Hz
            if (dt > 0.1) dt = 0.1;     // Min 10Hz
            freq_ = 1.0 / dt;
        }
        lastTime_ = timestamp;

        // Estimate derivative (velocity)
        double dx = 0.0;
        if (x_.lastValue() != 0.0 || !std::isnan(x_.lastValue())) {
            dx = (value - x_.lastValue()) * freq_;
        }
        
        double edx = dx_.filter(dx, alpha(dcutoff_));
        
        // Calculate cutoff based on estimated velocity
        double cutoff = mincutoff_ + beta_ * std::abs(edx);
        
        return x_.filter(value, alpha(cutoff));
    }

    void reset() {
        x_.reset();
        dx_.reset();
        lastTime_ = -1.0;
    }

private:
    double alpha(double cutoff) {
        double te = 1.0 / freq_;
        double tau = 1.0 / (2.0 * 3.14159265358979323846 * cutoff);
        return te / (te + tau);
    }

    double freq_;
    double mincutoff_;
    double beta_;
    double dcutoff_;
    double lastTime_;

    LowPassFilter x_;
    LowPassFilter dx_;
};

#endif // ONE_EURO_FILTER_H
