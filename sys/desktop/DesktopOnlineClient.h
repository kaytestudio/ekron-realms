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

    // In-game store economy. When the client is connected and the relay has
    // acknowledged the session (shopSynced), the relay is authoritative for
    // the gold-coin wallet and item ownership; the client mirrors that state.
    // Offline, the client falls back to its own persisted ShopState.
    long long walletCoins() const;
    bool shopOwns(int itemId) const;
    std::vector<int> shopOwned() const;
    bool shopSynced() const;
    // Request a purchase from the relay. No-op when offline/unsynced (returns
    // -1); otherwise returns a monotonic request id that is echoed back in the
    // "purchase" reply (see lastPurchaseReq) so the caller can correlate it.
    long long requestPurchase(int itemId);
    // Result of the most recent relay purchase reply (for UI feedback).
    int lastPurchaseId() const;
    bool lastPurchaseOk() const;
    std::string lastPurchaseReason() const;
    long long lastPurchaseReq() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
}
