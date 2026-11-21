import Foundation
import Network
import CoreFoundation

/// TLS presence only; sandbox building, vehicles and physics remain local.
final class OnlinePresence {
    var onPlayers: (([Float]) -> Void)?
    var onStatus: ((String) -> Void)?
    private struct Peer { var pose: [Float]?; var sequence: Int64 = -1 }
    private var peers: [String: Peer] = [:]
    private var connection: NWConnection?
    private var timer: DispatchSourceTimer?
    private var buffer = Data()
    private var epoch = 0
    private var welcomed = false
    private var sending = false
    private var seq: Int64 = 0
    private var pose: [Float]?

    func updatePose(_ status: [Float]) {
        guard status.count >= 17, Int(status[4]) == 6 else { pose = nil; return }
        pose = [status[0], status[1], status[2], status[15], status[16]]
    }

    func disconnect() {
        epoch += 1
        timer?.cancel(); timer = nil
        connection?.cancel(); connection = nil
        peers.removeAll(); buffer.removeAll(); pose = nil
        welcomed = false; sending = false; seq = 0
        onPlayers?([])
    }

    func connect(host: String, port: UInt16, name: String, token: String) {
        disconnect()
        let current = epoch
        onStatus?(loc("online_status_connecting"))
        let parameters = NWParameters(tls: NWProtocolTLS.Options(), tcp: NWProtocolTCP.Options())
        let conn = NWConnection(host: NWEndpoint.Host(host), port: NWEndpoint.Port(rawValue: port)!, using: parameters)
        connection = conn
        var started = false
        conn.stateUpdateHandler = { [weak self, weak conn] state in
            guard let self = self, let conn = conn, self.epoch == current, self.connection === conn else { return }
            switch state {
            case .ready:
                guard !started else { return }
                started = true
                // Default Network.framework trust and hostname verification remain enabled.
                self.send(["type": "hello", "version": 1, "name": name, "token": token], epoch: current)
                self.receive(conn, epoch: current)
                let timer = DispatchSource.makeTimerSource(queue: .main)
                timer.schedule(deadline: .now() + .milliseconds(100), repeating: .milliseconds(150))
                timer.setEventHandler { [weak self] in self?.tick(epoch: current) }
                self.timer = timer; timer.resume()
                DispatchQueue.main.asyncAfter(deadline: .now() + 5) { [weak self] in
                    guard let self = self, self.epoch == current, !self.welcomed else { return }
                    self.fail(loc("online_error_no_response"))
                }
            case .failed, .cancelled:
                self.fail(loc("online_error_closed_check"))
            default: break
            }
        }
        conn.start(queue: .main)
        DispatchQueue.main.asyncAfter(deadline: .now() + 10) { [weak self] in
            guard let self = self, self.epoch == current, !self.welcomed else { return }
            self.fail(loc("online_error_connect"))
        }
    }

    private func fail(_ message: String) {
        disconnect()
        onStatus?(message)
    }

    private func send(_ value: [String: Any], epoch current: Int) {
        guard current == epoch, let conn = connection, !sending,
              var data = try? JSONSerialization.data(withJSONObject: value) else { return }
        data.append(0x0a)
        guard data.count <= 4096 else { fail(loc("online_error_message_too_large")); return }
        sending = true
        conn.send(content: data, completion: .contentProcessed { [weak self] error in
            guard let self = self, self.epoch == current else { return }
            self.sending = false
            if error != nil { self.fail(loc("online_error_connection")) }
        })
    }

    private func tick(epoch current: Int) {
        guard current == epoch, welcomed, !sending else { return }
        if let p = pose, p.count == 5, p.allSatisfy({ $0.isFinite }),
           abs(p[0]) <= 512, (-100...200).contains(p[1]), abs(p[2]) <= 512,
           abs(p[3]) <= 1000000, abs(Double(p[4])) <= 1.3 {
            send(["type": "pose", "seq": seq, "position": [p[0], p[1], p[2]],
                  "yaw": p[3], "pitch": p[4]], epoch: current)
            seq += 1
        } else {
            send(["type": "ping"], epoch: current)
        }
    }

    private func receive(_ conn: NWConnection, epoch current: Int) {
        conn.receive(minimumIncompleteLength: 1, maximumLength: 4096) { [weak self, weak conn] data, _, complete, error in
            guard let self = self, let conn = conn, self.epoch == current, self.connection === conn else { return }
            if let data = data {
                self.buffer.append(data)
                while let newline = self.buffer.firstIndex(of: 0x0a) {
                    let line = self.buffer.prefix(upTo: newline)
                    guard line.count < 16384, self.process(Data(line)) else { self.fail(loc("online_error_invalid_response")); return }
                    self.buffer.removeSubrange(...newline)
                }
                guard self.buffer.count < 16384 else { self.fail(loc("online_error_response_too_large")); return }
            }
            if error != nil || complete { self.fail(loc("online_status_closed")); return }
            self.receive(conn, epoch: current)
        }
    }

    private func number(_ value: Any?) -> Double? {
        guard let n = value as? NSNumber, CFGetTypeID(n) != CFBooleanGetTypeID() else { return nil }
        let v = n.doubleValue
        return v.isFinite ? v : nil
    }
    private func validID(_ id: String) -> Bool {
        id.range(of: "^[0-9a-f]{32}$", options: .regularExpression) != nil
    }
    private func decodePose(_ value: [String: Any]) -> ([Float], Int64)? {
        guard let pos = value["position"] as? [Any], pos.count == 3,
              let x = number(pos[0]), let y = number(pos[1]), let z = number(pos[2]),
              let yaw = number(value["yaw"]), let pitch = number(value["pitch"]),
              let sequence = number(value["seq"]), sequence >= 0, sequence <= 9007199254740991,
              sequence.rounded(.towardZero) == sequence,
              abs(x) <= 512, (-100...200).contains(y), abs(z) <= 512,
              abs(yaw) <= 1000000, abs(pitch) <= 1.3 else { return nil }
        return ([Float(x), Float(y), Float(z), Float(yaw)], Int64(sequence))
    }
    private func publish() { onPlayers?(peers.values.flatMap { $0.pose ?? [] }) }

    private func process(_ data: Data) -> Bool {
        var depth = 0, quoted = false, escaped = false
        for byte in data {
            if quoted {
                if escaped { escaped = false }
                else if byte == 0x5c { escaped = true }
                else if byte == 0x22 { quoted = false }
            } else if byte == 0x22 { quoted = true }
            else if byte == 0x7b || byte == 0x5b { depth += 1; if depth > 8 { return false } }
            else if byte == 0x7d || byte == 0x5d { depth -= 1; if depth < 0 { return false } }
        }
        guard !quoted, depth == 0 else { return false }
        guard let message = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any],
              let type = message["type"] as? String else { return false }
        switch type {
        case "welcome":
            guard !welcomed, number(message["version"]) == 1,
                  let own = message["id"] as? String, validID(own),
                  let list = message["players"] as? [[String: Any]], list.count <= 15 else { return false }
            for entry in list {
                guard let id = entry["id"] as? String, validID(id), id != own, peers[id] == nil else { return false }
                var peer = Peer()
                if let raw = entry["pose"] as? [String: Any] {
                    guard let (p, s) = decodePose(raw) else { return false }
                    peer.pose = p; peer.sequence = s
                } else if !(entry["pose"] is NSNull) { return false }
                peers[id] = peer
            }
            welcomed = true; publish()
            onStatus?(loc("online_status_connected"))
        case "joined":
            guard welcomed, let id = message["id"] as? String, validID(id), peers[id] == nil, peers.count < 15 else { return false }
            peers[id] = Peer(); publish()
        case "pose":
            guard welcomed, let id = message["id"] as? String, var peer = peers[id],
                  let (p, s) = decodePose(message), s > peer.sequence else { return false }
            peer.pose = p; peer.sequence = s; peers[id] = peer; publish()
        case "left":
            guard welcomed, let id = message["id"] as? String, peers.removeValue(forKey: id) != nil else { return false }
            publish()
        case "pong":
            guard welcomed else { return false }
        default: return false
        }
        return true
    }
}
