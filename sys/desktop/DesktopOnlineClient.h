#pragma once

#include "OnlinePresence.h"
#include <memory>
#include <string>

namespace ekron::desktop {
// Optional experimental TLS presence client. Never transmits local sandbox saves.
class DesktopOnlineClient {
public:
    DesktopOnlineClient();
    ~DesktopOnlineClient();
    DesktopOnlineClient(const DesktopOnlineClient &) = delete;
    DesktopOnlineClient &operator=(const DesktopOnlineClient &) = delete;
    void connect(std::string host, int port, std::string name, std::string token);
    void disconnect();
    void updatePose(float x, float y, float z, float yaw, float pitch);
    std::vector<sandbox::RemotePlayerPose> players() const;
    std::string status() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
