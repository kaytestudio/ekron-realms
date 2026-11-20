#include "../../sys/android/app/src/main/cpp/OnlinePresence.h"
#include <cassert>
#include <limits>
#include <thread>
#include <iostream>

int main() {
    sandbox::RemotePlayers players;
    const float good[] = {1, 1.7f, 3, 0, -2, 15, 4, 1};
    assert(players.replace(good, 8));
    auto snapshot = players.snapshot();
    assert(snapshot.size() == 2 && snapshot[0].x == 1 && snapshot[1].y == 15);
    std::vector<sandbox::MeshVertex> mesh;
    sandbox::appendRemotePlayers(mesh, snapshot);
    assert(mesh.size() == 2 * 5 * 36);
    const float bad[] = {0, std::numeric_limits<float>::quiet_NaN(), 0, 0};
    assert(!players.replace(bad, 4));
    assert(!players.replace(good, 3));
    assert(!players.replace(nullptr, 4));
    assert(players.snapshot().size() == 2);
    std::thread writer([&] { for (int i = 0; i < 1000; ++i) assert(players.replace(good, 8)); });
    for (int i = 0; i < 1000; ++i) assert(players.snapshot().size() == 2);
    writer.join();
    assert(players.replace(nullptr, 0));
    assert(players.snapshot().empty());
    std::cout << "Remote player bounds, rendering and concurrent snapshots OK\n";
}
