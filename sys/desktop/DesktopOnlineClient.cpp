#include "DesktopOnlineClient.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

#if defined(EKRON_TLS_ONLINE) && !defined(_WIN32)
#include "third_party/json.hpp"
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <map>
#endif

namespace ekron::desktop {
struct DesktopOnlineClient::State {
    std::atomic<bool> stopped{false};
    mutable std::mutex mutex;
    std::string status = "Offline";
    std::array<float, 5> pose{};
    bool hasPose = false;
    int socket = -1;
    sandbox::RemotePlayers remote;
};

DesktopOnlineClient::DesktopOnlineClient() : state_(std::make_shared<State>()) {}
DesktopOnlineClient::~DesktopOnlineClient() { disconnect(); }
void DesktopOnlineClient::disconnect() {
    const auto state = state_;
    state->stopped = true;
#if defined(EKRON_TLS_ONLINE) && !defined(_WIN32)
    { std::lock_guard<std::mutex> lock(state->mutex); if (state->socket >= 0) ::shutdown(state->socket, SHUT_RDWR); }
#endif
    { std::lock_guard<std::mutex> lock(state->mutex); state->status = "Offline"; }
    state->remote.replace(nullptr, 0);
}
void DesktopOnlineClient::updatePose(float x, float y, float z, float yaw, float pitch) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->pose = {x, y, z, yaw, pitch};
    state_->hasPose = true;
}
std::vector<sandbox::RemotePlayerPose> DesktopOnlineClient::players() const { return state_->remote.snapshot(); }
std::string DesktopOnlineClient::status() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->status;
}

void DesktopOnlineClient::connect(std::string host, int port, std::string name, std::string token) {
    disconnect();
    state_ = std::make_shared<State>();
    auto state = state_;
#if defined(EKRON_TLS_ONLINE) && !defined(_WIN32)
    if (host.empty() || host.size() > 253 || port < 1 || port > 65535 || name.empty() || name.size() > 128 || token.size() < 32 || token.size() > 256) {
        std::lock_guard<std::mutex> lock(state->mutex); state->status = "Invalid online settings"; return;
    }
    { std::lock_guard<std::mutex> lock(state->mutex); state->status = "Connecting"; }
    // Detached worker owns State; shutdown never waits for a blocking DNS lookup.
    std::thread([state, host = std::move(host), port, name = std::move(name), token = std::move(token)]() {
        // A peer closing its socket must not terminate the game via SIGPIPE.
        sigset_t signals; sigemptyset(&signals); sigaddset(&signals, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &signals, nullptr);
        using Clock = std::chrono::steady_clock;
        using Json = nlohmann::json;
        int fd = -1;
        SSL_CTX *ctx = nullptr;
        SSL *ssl = nullptr;
        bool welcomed = false;
        auto setStatus = [&](const char *text) { std::lock_guard<std::mutex> lock(state->mutex); state->status = text; };
        auto wait = [&](short events, Clock::time_point deadline) {
            while (!state->stopped && Clock::now() < deadline) {
                pollfd p{fd, events, 0};
                int n = ::poll(&p, 1, 100);
                if (n > 0) return (p.revents & events) != 0;
                if (n < 0 && errno != EINTR) return false;
            }
            return false;
        };
        auto send = [&](const std::string &data) {
            if (data.size() > 4096 || !ssl) return false;
            const auto deadline = Clock::now() + std::chrono::seconds(3);
            size_t offset = 0;
            while (offset < data.size() && !state->stopped) {
                int n = SSL_write(ssl, data.data() + offset, int(data.size() - offset));
                if (n > 0) { offset += size_t(n); continue; }
                int e = SSL_get_error(ssl, n);
                if (e == SSL_ERROR_WANT_READ || e == SSL_ERROR_WANT_WRITE) {
                    if (!wait(e == SSL_ERROR_WANT_READ ? POLLIN : POLLOUT, deadline)) return false;
                } else return false;
            }
            return offset == data.size();
        };
        struct Peer { bool hasPose = false; long long seq = -1; std::array<float, 4> pose{}; };
        std::map<std::string, Peer> peers;
        auto validId = [](const std::string &id) {
            if (id.size() != 32) return false;
            for (char c : id) if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
            return true;
        };
        auto parsePose = [&](const Json &j, Peer &peer) {
            if (!j.contains("seq") || !j["seq"].is_number_integer() || !j.contains("position") || !j["position"].is_array() || j["position"].size() != 3 || !j.contains("yaw") || !j.contains("pitch")) return false;
            const auto seq = j["seq"].get<long long>();
            if (seq < 0 || seq <= peer.seq || seq > 9007199254740991LL) return false;
            const auto &p = j["position"];
            if (!p[0].is_number() || !p[1].is_number() || !p[2].is_number() || !j["yaw"].is_number() || !j["pitch"].is_number()) return false;
            double x=p[0].get<double>(), y=p[1].get<double>(), z=p[2].get<double>(), yaw=j["yaw"].get<double>(), pitch=j["pitch"].get<double>();
            if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(yaw) || !std::isfinite(pitch) || std::abs(x)>512 || y< -100 || y>200 || std::abs(z)>512 || std::abs(yaw)>1000000 || std::abs(pitch)>1.3) return false;
            peer.seq=seq;peer.hasPose=true;peer.pose={float(x),float(y),float(z),float(yaw)};return true;
        };
        auto publish = [&]() {
            std::vector<float> packed;
            for (const auto &p : peers) if (p.second.hasPose) packed.insert(packed.end(),p.second.pose.begin(),p.second.pose.end());
            state->remote.replace(packed.data(), packed.size());
        };
        auto process = [&](const std::string &line) {
            int depth = 0; bool quoted = false, escaped = false;
            for (char c : line) {
                if (quoted) {
                    if (escaped) escaped = false;
                    else if (c == '\\') escaped = true;
                    else if (c == '"') quoted = false;
                } else if (c == '"') quoted = true;
                else if (c == '{' || c == '[') { if (++depth > 8) return false; }
                else if (c == '}' || c == ']') { if (--depth < 0) return false; }
            }
            if (quoted || depth) return false;
            try {
                auto j=Json::parse(line);
                if (!j.is_object() || !j.contains("type") || !j["type"].is_string()) return false;
                auto type=j["type"].get<std::string>();
                if (type=="welcome") {
                    if (welcomed || !j.contains("version") || !j["version"].is_number_integer() || j["version"].get<int>()!=1 || !j.contains("id") || !j["id"].is_string() || !validId(j["id"].get<std::string>()) || !j.contains("players") || !j["players"].is_array() || j["players"].size()>15) return false;
                    const auto own=j["id"].get<std::string>();
                    for (const auto &p:j["players"]) {
                        if (!p.is_object() || !p.contains("id") || !p["id"].is_string() || !p.contains("pose")) return false;
                        auto id=p["id"].get<std::string>();
                        if (!validId(id)||id==own||peers.count(id)) return false;
                        Peer peer;
                        if (!p["pose"].is_null() && (!p["pose"].is_object() || !parsePose(p["pose"],peer))) return false;
                        peers.emplace(id,peer);
                    }
                    welcomed=true;publish();setStatus("Online: players only");return true;
                }
                if (!welcomed) return false;
                if (type=="pong") return true;
                if (!j.contains("id") || !j["id"].is_string()) return false;
                auto id=j["id"].get<std::string>();
                if (type=="joined") {
                    if (!validId(id)||peers.count(id)||peers.size()>=15) return false;
                    peers.emplace(id,Peer{});publish();return true;
                }
                auto it=peers.find(id);
                if (it==peers.end()) return false;
                if (type=="left") { peers.erase(it);publish();return true; }
                if (type=="pose" && parsePose(j,it->second)) { publish();return true; }
            } catch (const std::exception &) { }
            return false;
        };
        do {
            addrinfo hints{};hints.ai_family=AF_UNSPEC;hints.ai_socktype=SOCK_STREAM;
            addrinfo *result=nullptr;
            if (::getaddrinfo(host.c_str(),std::to_string(port).c_str(),&hints,&result)!=0) break;
            const auto deadline=Clock::now()+std::chrono::seconds(5);
            for (auto *a=result;a && !state->stopped && Clock::now()<deadline;a=a->ai_next) {
                fd=::socket(a->ai_family,a->ai_socktype,a->ai_protocol);
                if (fd<0) continue;
                if (::fcntl(fd,F_SETFL,::fcntl(fd,F_GETFL,0)|O_NONBLOCK)<0) {::close(fd);fd=-1;continue;}
                {std::lock_guard<std::mutex> lock(state->mutex);state->socket=fd;}
                int rc=::connect(fd,a->ai_addr,a->ai_addrlen);
                if (rc==0 || (errno==EINPROGRESS && wait(POLLOUT,deadline))) {
                    int error=0;socklen_t length=sizeof(error);
                    if (::getsockopt(fd,SOL_SOCKET,SO_ERROR,&error,&length)==0 && error==0) break;
                }
                {std::lock_guard<std::mutex> lock(state->mutex);state->socket=-1;}
                ::close(fd);fd=-1;
            }
            ::freeaddrinfo(result);
            if (fd<0 || state->stopped) break;
            ctx=SSL_CTX_new(TLS_client_method());
            if (!ctx || SSL_CTX_set_min_proto_version(ctx,TLS1_2_VERSION)!=1 || SSL_CTX_set_default_verify_paths(ctx)!=1) break;
            SSL_CTX_set_verify(ctx,SSL_VERIFY_PEER,nullptr);
            ssl=SSL_new(ctx);
            if (!ssl || SSL_set_fd(ssl,fd)!=1) break;
            in_addr ip4{};in6_addr ip6{};
            if (::inet_pton(AF_INET,host.c_str(),&ip4)==1 || ::inet_pton(AF_INET6,host.c_str(),&ip6)==1) {
                if (X509_VERIFY_PARAM_set1_ip_asc(SSL_get0_param(ssl),host.c_str())!=1) break;
            } else {
                if (SSL_set1_host(ssl,host.c_str())!=1 || SSL_set_tlsext_host_name(ssl,host.c_str())!=1) break;
            }
            const auto handshakeDeadline=Clock::now()+std::chrono::seconds(5);
            bool handshake=false;
            while (!state->stopped) {
                int n=SSL_connect(ssl);
                if (n==1) {handshake=true;break;}
                int error=SSL_get_error(ssl,n);
                if ((error!=SSL_ERROR_WANT_READ && error!=SSL_ERROR_WANT_WRITE) || !wait(error==SSL_ERROR_WANT_READ?POLLIN:POLLOUT,handshakeDeadline)) break;
            }
            if (!handshake || SSL_get_verify_result(ssl)!=X509_V_OK) break;
            try {
                if (!send(Json{{"type","hello"},{"version",1},{"name",name},{"token",token}}.dump()+"\n")) break;
            } catch (const std::exception &) { break; }
            std::string input;long long seq=0;
            auto nextSend=Clock::now(), welcomeDeadline=Clock::now()+std::chrono::seconds(5);
            bool ok=true;
            while (!state->stopped && ok) {
                auto now=Clock::now();
                if (!welcomed && now>welcomeDeadline) break;
                if (welcomed && now>=nextSend) {
                    std::array<float,5> p{};bool hasPose=false;
                    {std::lock_guard<std::mutex> lock(state->mutex);p=state->pose;hasPose=state->hasPose;}
                    Json message={{"type","ping"}};
                    if (hasPose && std::all_of(p.begin(),p.end(),[](float v){return std::isfinite(v);}) &&
                        std::abs(p[0]) <= 512 && p[1] >= -100 && p[1] <= 200 && std::abs(p[2]) <= 512 &&
                        std::abs(p[3]) <= 1000000 && std::abs(double(p[4])) <= 1.3)
                        message={{"type","pose"},{"seq",seq++},{"position",{p[0],p[1],p[2]}},{"yaw",p[3]},{"pitch",p[4]}};
                    if (!send(message.dump()+"\n")) break;
                    nextSend=now+std::chrono::milliseconds(150);
                }
                if (SSL_pending(ssl)>0 || wait(POLLIN,Clock::now()+std::chrono::milliseconds(100))) {
                    char bytes[4096];int n=SSL_read(ssl,bytes,sizeof(bytes));
                    if (n<=0) {int e=SSL_get_error(ssl,n);if(e==SSL_ERROR_WANT_READ||e==SSL_ERROR_WANT_WRITE)continue;break;}
                    for (int i=0;i<n;i++) {
                        if (bytes[i]=='\n') {if (!process(input)){ok=false;break;}input.clear();}
                        else {input.push_back(bytes[i]);if(input.size()>=16384){ok=false;break;}}
                    }
                }
            }
        } while (false);
        if (ssl) SSL_free(ssl);
        if (ctx) SSL_CTX_free(ctx);
        if (fd>=0) {
            { std::lock_guard<std::mutex> lock(state->mutex);state->socket=-1; }
            ::close(fd);
        }
        state->remote.replace(nullptr,0);
        if (!state->stopped) setStatus(welcomed ? "Connection closed" : "Connection failed (TLS or credentials)");
    }).detach();
#else
    (void)host;(void)port;(void)name;(void)token;
    std::lock_guard<std::mutex> lock(state->mutex);state->status="Online unavailable: OpenSSL missing";
#endif
}
} // namespace ekron::desktop
