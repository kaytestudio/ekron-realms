// Yamagi Quake II's cl_showfps (cl_screen.c SCR_Framecounter): the frame rate
// over the last 60 frames, drawn at the top right of the screen.
//   cl_showfps 0  nothing (the default)
//              1  the average:         "59.94fps"
//              2  slowest, fastest and average:
//                 "Min:   58.10fps, Max:   61.20fps, Avg:   59.94fps"
//              3  that, and the frame times under it:
//                 "Max: 17.21ms, Min: 16.34ms, Avg: 16.68ms"
// Values between 1 and 2 count as 1.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace realms {

class FrameCounter {
public:
    static constexpr int kFrames = 60; // SCR_Framecounter's frametimes[60]

    void reset() {
        times_.fill(0.0);
        next_ = 0;
        last_ = -1.0;
    }
    // A frame drawn at `seconds` (any steady clock). The first one only
    // starts the timing; a clock that goes back starts it again.
    void frame(double seconds) {
        if (!std::isfinite(seconds)) return;
        if (last_ >= 0.0 && seconds > last_) {
            times_[size_t(next_)] = seconds - last_;
            next_ = (next_ + 1) % kFrames;
        }
        last_ = seconds;
    }
    int count() const { return int(std::count_if(times_.begin(), times_.end(), [](double t) { return t > 0.0; })); }
    // Frame times in seconds over the counted frames; 0 without any.
    double average() const {
        double sum = 0.0;
        int n = 0;
        for (double t : times_)
            if (t > 0.0) sum += t, n++;
        return n ? sum / n : 0.0;
    }
    double shortest() const {
        double best = 0.0;
        for (double t : times_)
            if (t > 0.0 && (best == 0.0 || t < best)) best = t;
        return best;
    }
    double longest() const { return *std::max_element(times_.begin(), times_.end()); }

    // The cvar's lines, top first; none for cl_showfps below 1 or before two
    // frames have been timed.
    std::vector<std::string> lines(float showfps) const {
        std::vector<std::string> out;
        if (!std::isfinite(showfps) || showfps < 1.0f || count() == 0) return out;
        char buf[96];
        const double avg = average(), slow = longest(), fast = shortest();
        if (showfps < 2.0f) {
            std::snprintf(buf, sizeof(buf), "%3.2ffps", 1.0 / avg);
            out.emplace_back(buf);
            return out;
        }
        std::snprintf(buf, sizeof(buf), "Min: %7.2ffps, Max: %7.2ffps, Avg: %7.2ffps", 1.0 / slow, 1.0 / fast,
                      1.0 / avg);
        out.emplace_back(buf);
        if (showfps > 2.0f) {
            std::snprintf(buf, sizeof(buf), "Max: %5.2fms, Min: %5.2fms, Avg: %5.2fms", 1000.0 * slow, 1000.0 * fast,
                          1000.0 * avg);
            out.emplace_back(buf);
        }
        return out;
    }

private:
    std::array<double, kFrames> times_{};
    int next_ = 0;
    double last_ = -1.0;
};

} // namespace realms
