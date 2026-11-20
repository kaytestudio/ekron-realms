#include "DesktopOnlineClient.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
template<class Predicate> bool waitFor(Predicate predicate) {
    const auto end = std::chrono::steady_clock::now() + 5s;
    while (std::chrono::steady_clock::now() < end) {
        if (predicate()) return true;
        std::this_thread::sleep_for(20ms);
    }
    return predicate();
}
int main(int argc, char **argv) {
    if (argc != 5) return 2;
    const std::string host = argv[1], token = argv[3], mode = argv[4];
    const int port = std::stoi(argv[2]);
    ekron::desktop::DesktopOnlineClient first;
    first.updatePose(1, 1.7f, 3, .4f, .2f);
    first.connect(host, port, "Alice", token);
    // connect resets the state, so send the latest pose after connecting.
    first.updatePose(1, 1.7f, 3, .4f, .2f);
    if (mode == "reject") {
        if (!waitFor([&] { return first.status().find("failed") != std::string::npos; })) return 3;
        return 0;
    }
    if (!waitFor([&] { return first.status() == "Online: players only"; })) {
        std::cerr << "first: " << first.status() << '\n'; return 4;
    }
    ekron::desktop::DesktopOnlineClient second;
    second.connect(host, port, "Bob", token);
    second.updatePose(-4, 2, 5, -.3f, -.1f);
    if (!waitFor([&] {
        auto a=first.players(), b=second.players();
        return a.size()==1 && b.size()==1 && std::abs(a[0].x+4)<.001 && std::abs(b[0].x-1)<.001;
    })) { std::cerr << "poses: " << first.status() << " / " << second.status() << '\n'; return 5; }
    second.disconnect();
    if (!waitFor([&] { return first.players().empty(); })) return 6;
    first.disconnect();
    return 0;
}
