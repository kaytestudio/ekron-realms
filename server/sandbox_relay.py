#!/usr/bin/env python3
"""Experimental TLS presence relay for render-only sandbox player markers.

This deliberately handles presence and player transforms only, plus a
per-player in-game store economy (gold-coin wallet, passive coin accrual and
authoritative skin/unlock purchases). It is not an authoritative sandbox
simulation or a production-ready public game server.
"""

import argparse
import asyncio
import hmac
import json
import math
import os
import secrets
import ssl
import time
from collections import deque

MAX_FRAME = 4096  # client-to-server, including newline
MAX_SERVER_FRAME = 16384  # a welcome can contain up to 15 existing players
MAX_PEERS = 16
MAX_PENDING_BYTES = 65536
AUTH_TIMEOUT = 5.0
IDLE_TIMEOUT = 30.0
SEND_TIMEOUT = 3.0
MAX_MESSAGES_PER_SECOND = 30

# ---------------------------------------------------------------------------
# In-game store economy (weapon skins + gold-coin unlocks).
#
# The relay is the authoritative wallet while a client is connected: it holds
# each peer's gold-coin balance and owned item ids, accrues passive coins for
# connection time, and validates purchases against the shared price table.
# Item ids/prices mirror sys/desktop/ShopCatalog.cpp.
# ---------------------------------------------------------------------------
START_COINS = 500
COIN_RATE_PER_SECOND = 2.0

SHOP_PRICES = {
    1: 150, 2: 150, 3: 200, 4: 200, 5: 350, 6: 350, 7: 400, 8: 500,
    101: 300, 102: 500, 103: 250, 104: 250, 105: 400, 106: 800,
}
# Items that grant an immediate coin bonus when purchased (mirrors the client
# Coin Bonus unlock). Applied server-side so the authoritative balance stays
# consistent with the offline effect.
SHOP_COIN_BONUS = {101: 500}
# Item that doubles the passive accrual rate (mirrors the client Double Coins).
SHOP_DOUBLE_COINS = 102


class ProtocolError(Exception):
    pass


def no_duplicates(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ProtocolError("duplicate key")
        result[key] = value
    return result


def decode_frame(line):
    if not line.endswith(b"\n") or len(line) > MAX_FRAME:
        raise ProtocolError("invalid frame size")
    try:
        value = json.loads(line.decode("utf-8"), object_pairs_hook=no_duplicates,
                           parse_constant=lambda _: (_ for _ in ()).throw(ProtocolError("non-finite number")))
    except (UnicodeDecodeError, ValueError, RecursionError) as exc:
        raise ProtocolError("invalid JSON") from exc
    if not isinstance(value, dict):
        raise ProtocolError("message must be an object")
    return value


def bounded_number(value, limit):
    if type(value) not in (int, float):
        raise ProtocolError("expected number")
    try:
        number = float(value)
    except OverflowError as exc:
        raise ProtocolError("number out of bounds") from exc
    if not math.isfinite(number) or abs(number) > limit:
        raise ProtocolError("number out of bounds")
    return number


def validate_pose(message, previous_seq):
    if set(message) != {"type", "seq", "position", "yaw", "pitch"}:
        raise ProtocolError("invalid pose fields")
    seq = message["seq"]
    if type(seq) is not int or not previous_seq < seq <= 2**53 - 1:
        raise ProtocolError("invalid sequence")
    position = message["position"]
    if not isinstance(position, list) or len(position) != 3:
        raise ProtocolError("invalid position")
    x, y, z = (bounded_number(v, 512) for v in position)
    if not -100 <= y <= 200:
        raise ProtocolError("invalid height")
    yaw = bounded_number(message["yaw"], 1000000)
    pitch = bounded_number(message["pitch"], 1.3)
    return {"type": "pose", "seq": seq, "position": [x, y, z],
            "yaw": yaw, "pitch": pitch}


def encode(message):
    return (json.dumps(message, ensure_ascii=True, separators=(",", ":"),
                       allow_nan=False) + "\n").encode("ascii")


def wallet_message(peer):
    return {"type": "wallet", "coins": peer.coins, "owned": sorted(peer.owned)}


def accrue_coins(peer, now):
    """Grant passive coins for connection time. Returns True when the balance
    changed (so callers can decide whether to notify the client). The Double
    Coins unlock doubles the rate, matching the client-side effect."""
    elapsed = now - peer.last_reward
    if elapsed <= 0.0:
        return False
    rate = COIN_RATE_PER_SECOND * (2 if SHOP_DOUBLE_COINS in peer.owned else 1)
    gained = int(elapsed * rate)
    if gained <= 0:
        return False
    peer.coins += gained
    # Keep the fractional remainder so accrual stays accurate across calls.
    peer.last_reward += gained / rate
    return True


class Peer:
    def __init__(self, writer, name):
        self.id = secrets.token_hex(16)
        self.writer = writer
        self.name = name
        self.seq = -1
        self.pose = None
        self.messages = deque()
        self.coins = START_COINS
        self.owned = set()
        self.last_reward = time.monotonic()


class PresenceRelay:
    def __init__(self, token, max_peers=MAX_PEERS):
        if not isinstance(token, str) or len(token.encode("utf-8")) < 32 or len(token.encode("utf-8")) > 256:
            raise ValueError("join token must be between 32 and 256 UTF-8 bytes")
        if not 1 <= max_peers <= MAX_PEERS:
            raise ValueError("invalid peer limit")
        self.token = token.encode("utf-8")
        self.max_peers = max_peers
        self.peers = {}
        self.connections = 0

    async def send(self, writer, message):
        try:
            if writer.is_closing():
                return
            if writer.transport.get_write_buffer_size() > MAX_PENDING_BYTES:
                writer.close()
                return
            frame = encode(message)
            if len(frame) > MAX_SERVER_FRAME:
                writer.close()
                return
            writer.write(frame)
            await asyncio.wait_for(writer.drain(), SEND_TIMEOUT)
        except (asyncio.TimeoutError, ConnectionError, OSError):
            writer.close()

    async def broadcast(self, message, exclude=None):
        # Timeouts and buffer limits prevent one slow client blocking everyone.
        await asyncio.gather(*(self.send(peer.writer, message)
                               for peer in list(self.peers.values()) if peer.id != exclude))

    async def handle_purchase(self, peer, message):
        # Authoritative store purchase: validate the item and the wallet, then
        # reply with the resulting balance and ownership so the client can
        # mirror it. Economy state is per-peer and never broadcast.
        item_id = message.get("id")
        if type(item_id) is not int or item_id not in SHOP_PRICES:
            raise ProtocolError("invalid shop item")
        req = message.get("req")
        if type(req) is not int:
            req = None
        price = SHOP_PRICES[item_id]
        owned = sorted(peer.owned)
        if item_id in peer.owned:
            await self.send(writer=peer.writer, message={
                "type": "purchase", "id": item_id, "ok": False,
                "reason": "owned", "coins": peer.coins, "owned": owned, "req": req})
            return
        if peer.coins < price:
            await self.send(writer=peer.writer, message={
                "type": "purchase", "id": item_id, "ok": False,
                "reason": "insufficient", "coins": peer.coins, "owned": owned, "req": req})
            return
        peer.coins -= price
        peer.owned.add(item_id)
        # Coin Bonus unlock grants an immediate top-up, mirroring the client.
        if item_id in SHOP_COIN_BONUS:
            peer.coins += SHOP_COIN_BONUS[item_id]
        await self.send(writer=peer.writer, message={
            "type": "purchase", "id": item_id, "ok": True,
            "coins": peer.coins, "owned": sorted(peer.owned), "req": req})

    async def read(self, reader, timeout):
        try:
            line = await asyncio.wait_for(reader.readline(), timeout)
        except (ValueError, asyncio.LimitOverrunError) as exc:
            raise ProtocolError("frame too large") from exc
        if not line:
            raise EOFError
        return decode_frame(line)

    async def handle(self, reader, writer):
        peer = None
        # Count unauthenticated clients too, to bound slow handshakes after TLS.
        if self.connections >= self.max_peers:
            writer.close()
            try:
                await asyncio.wait_for(writer.wait_closed(), SEND_TIMEOUT)
            except (asyncio.TimeoutError, ConnectionError, OSError):
                pass
            return
        self.connections += 1
        try:
            hello = await self.read(reader, AUTH_TIMEOUT)
            if set(hello) != {"type", "version", "token", "name"} or hello["type"] != "hello" or type(hello["version"]) is not int or hello["version"] != 1:
                raise ProtocolError("invalid hello")
            token, name = hello["token"], hello["name"]
            if not isinstance(token, str) or len(token) > 256 or not hmac.compare_digest(token.encode("utf-8"), self.token):
                raise ProtocolError("authentication failed")
            if not isinstance(name, str) or not 1 <= len(name) <= 32 or any(ord(c) < 32 or ord(c) == 127 for c in name):
                raise ProtocolError("invalid name")
            peer = Peer(writer, name)
            # There is no await between snapshot and registration: joins are ordered.
            previous_peers = list(self.peers.values())
            existing = [{"id": p.id, "name": p.name, "pose": p.pose}
                        for p in previous_peers]
            self.peers[peer.id] = peer
            await self.send(writer, {"type": "welcome", "version": 1, "id": peer.id,
                                     "players": existing, "wallet": wallet_message(peer)})
            # Newer joins already see this peer in their welcome snapshot.
            await asyncio.gather(*(self.send(p.writer, {"type": "joined", "id": peer.id, "name": peer.name})
                                   for p in previous_peers))
            while not writer.is_closing():
                message = await self.read(reader, IDLE_TIMEOUT)
                now = time.monotonic()
                accrue_coins(peer, now)
                while peer.messages and now - peer.messages[0] >= 1.0:
                    peer.messages.popleft()
                if len(peer.messages) >= MAX_MESSAGES_PER_SECOND:
                    raise ProtocolError("rate limit")
                peer.messages.append(now)
                if message == {"type": "ping"}:
                    await self.send(writer, {"type": "pong"})
                elif message.get("type") == "pose":
                    pose = validate_pose(message, peer.seq)
                    peer.seq = pose["seq"]
                    peer.pose = pose
                    await self.broadcast({**pose, "id": peer.id}, exclude=peer.id)
                elif message.get("type") == "wallet":
                    await self.send(writer, wallet_message(peer))
                elif message.get("type") == "buy":
                    await self.handle_purchase(peer, message)
                else:
                    raise ProtocolError("unsupported message")
        except (ProtocolError, EOFError, asyncio.TimeoutError, ConnectionError, OSError, UnicodeEncodeError):
            # Avoid echoing untrusted data or secrets into logs and error responses.
            pass
        finally:
            removed = peer is not None and self.peers.pop(peer.id, None) is not None
            self.connections -= 1
            writer.close()
            try:
                if removed:
                    await self.broadcast({"type": "left", "id": peer.id}, exclude=peer.id)
            finally:
                try:
                    await asyncio.wait_for(writer.wait_closed(), SEND_TIMEOUT)
                except (asyncio.TimeoutError, ConnectionError, OSError):
                    pass


async def serve(args, token):
    relay = PresenceRelay(token, args.max_peers)
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.minimum_version = ssl.TLSVersion.TLSv1_2
    context.load_cert_chain(args.cert, args.key)
    server = await asyncio.start_server(relay.handle, args.host, args.port, ssl=context,
                                        ssl_handshake_timeout=AUTH_TIMEOUT, limit=MAX_FRAME)
    async with server:
        print(f"Experimental presence relay listening on {args.host}:{args.port}", flush=True)
        await server.serve_forever()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1", help="listen address (default: localhost)")
    parser.add_argument("--port", type=int, default=38475)
    parser.add_argument("--cert", required=True, help="TLS certificate chain PEM")
    parser.add_argument("--key", required=True, help="TLS private key PEM")
    parser.add_argument("--max-peers", type=int, default=MAX_PEERS)
    args = parser.parse_args()
    token = os.environ.get("EKRON_JOIN_TOKEN", "")
    try:
        asyncio.run(serve(args, token))
    except (ValueError, OSError, ssl.SSLError) as exc:
        parser.exit(2, f"Unable to start relay: {exc}\n")
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
