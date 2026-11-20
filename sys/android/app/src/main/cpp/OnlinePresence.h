#pragma once

#include "MeshGeometry.h"
#include <cmath>
#include <mutex>
#include <vector>

namespace sandbox {
// Render-only remote players. These do not participate in collision or gameplay.
struct RemotePlayerPose { float x, y, z, yaw; };

class RemotePlayers {
public:
    static constexpr size_t maxPlayers = 15;

    // The platform networking thread supplies x, y, z, yaw tuples. Reject an
    // entire invalid update instead of allowing untrusted data into rendering.
    bool replace(const float *values, size_t count) {
        if (count % 4 || count > maxPlayers * 4 || (count && !values)) return false;
        std::vector<RemotePlayerPose> next;
        next.reserve(count / 4);
        for (size_t i = 0; i < count; i += 4) {
            const float x = values[i], y = values[i + 1], z = values[i + 2], yaw = values[i + 3];
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(yaw) ||
                std::abs(x) > 512 || y < -100 || y > 200 || std::abs(z) > 512 || std::abs(yaw) > 1000000)
                return false;
            next.push_back({x, y, z, yaw});
        }
        std::lock_guard<std::mutex> lock(mutex_);
        players_ = std::move(next);
        return true;
    }

    std::vector<RemotePlayerPose> snapshot() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return players_;
    }
private:
    mutable std::mutex mutex_;
    std::vector<RemotePlayerPose> players_;
};

inline void appendRemotePlayers(std::vector<MeshVertex> &vertices, const std::vector<RemotePlayerPose> &players) {
    for (const auto &p : players) {
        // The protocol transmits camera/eye height, not feet height.
        appendCube(vertices, {p.x, p.y - .65f, p.z, .48f, .9f, .28f, .22f, .58f, .65f, Plain});
        appendCube(vertices, {p.x, p.y - .08f, p.z, .32f, .32f, .32f, .85f, .74f, .58f, Plain});
        const float fx = -std::sin(p.yaw) * .19f, fz = -std::cos(p.yaw) * .19f;
        appendCube(vertices, {p.x + fx, p.y - .08f, p.z + fz, .10f, .08f, .10f, .08f, .08f, .08f, Plain});
        appendCube(vertices, {p.x - .13f, p.y - 1.35f, p.z, .19f, .65f, .22f, .20f, .24f, .28f, Plain});
        appendCube(vertices, {p.x + .13f, p.y - 1.35f, p.z, .19f, .65f, .22f, .20f, .24f, .28f, Plain});
    }
}
} // namespace sandbox
