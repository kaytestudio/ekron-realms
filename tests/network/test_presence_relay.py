import asyncio
import json
import pathlib
import ssl
import subprocess
import tempfile
import unittest

from server.sandbox_relay import MAX_FRAME, MAX_SERVER_FRAME, PresenceRelay, decode_frame, ProtocolError, encode

TOKEN = "correct horse battery staple 0123456789"


class RelayProtocolTests(unittest.TestCase):
    def test_invalid_json_is_rejected(self):
        for frame in (b'{}', b'[]\n', b'{"type":"a","type":"b"}\n',
                      b'{"x":NaN}\n', b'\xff\n', b'x' * MAX_FRAME + b'\n'):
            with self.subTest(frame=frame[:30]), self.assertRaises(ProtocolError):
                decode_frame(frame)

    def test_full_welcome_fits_server_frame_limit(self):
        pose = {"type": "pose", "seq": 2**53 - 1, "position": [512.0, 200.0, -512.0],
                "yaw": 1000000.0, "pitch": -1.3}
        players = [{"id": "f" * 32, "name": "😀" * 32, "pose": pose} for _ in range(15)]
        frame = encode({"type": "welcome", "version": 1, "id": "f" * 32, "players": players})
        self.assertLessEqual(len(frame), MAX_SERVER_FRAME)

    def test_token_is_mandatory_and_long(self):
        for token in ("", "small", "x" * 257):
            with self.subTest(token=token[:10]), self.assertRaises(ValueError):
                PresenceRelay(token)


class RelayNetworkTests(unittest.IsolatedAsyncioTestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.cert = pathlib.Path(cls.tmp.name) / "cert.pem"
        cls.key = pathlib.Path(cls.tmp.name) / "key.pem"
        subprocess.run(["openssl", "req", "-x509", "-newkey", "rsa:2048", "-nodes",
                        "-keyout", str(cls.key), "-out", str(cls.cert), "-days", "1",
                        "-subj", "/CN=localhost", "-addext", "subjectAltName=DNS:localhost"],
                       check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    async def asyncSetUp(self):
        self.relay = PresenceRelay(TOKEN)
        self.server_ssl = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.server_ssl.load_cert_chain(self.cert, self.key)
        self.server = await asyncio.start_server(self.relay.handle, "127.0.0.1", 0,
                                                 ssl=self.server_ssl, limit=MAX_FRAME)
        self.port = self.server.sockets[0].getsockname()[1]
        self.client_ssl = ssl.create_default_context(cafile=str(self.cert))
        self.writers = []

    async def asyncTearDown(self):
        for writer in self.writers:
            writer.close()
        for writer in self.writers:
            try:
                await writer.wait_closed()
            except (ConnectionError, OSError):
                pass
        self.server.close()
        await self.server.wait_closed()
        # Give server handlers a chance to finish the disconnect broadcasts.
        for _ in range(30):
            if not self.relay.connections:
                break
            await asyncio.sleep(0.01)
        self.assertEqual(self.relay.connections, 0)

    async def connect(self, name, token=TOKEN):
        reader, writer = await asyncio.open_connection("127.0.0.1", self.port,
                                                       ssl=self.client_ssl, server_hostname="localhost")
        self.writers.append(writer)
        await self.send(writer, {"type": "hello", "version": 1, "token": token, "name": name})
        return reader, writer

    async def send(self, writer, message):
        writer.write(encode(message))
        await writer.drain()

    async def recv(self, reader):
        data = await asyncio.wait_for(reader.readline(), 2)
        return json.loads(data) if data else None

    async def test_tls_join_pose_late_join_and_disconnect(self):
        a, aw = await self.connect("Alice")
        welcome_a = await self.recv(a)
        self.assertEqual(welcome_a["players"], [])
        b, bw = await self.connect("Bob")
        welcome_b = await self.recv(b)
        self.assertEqual(welcome_b["players"][0]["id"], welcome_a["id"])
        joined_b = await self.recv(a)
        self.assertEqual(joined_b["id"], welcome_b["id"])
        pose = {"type": "pose", "seq": 0, "position": [1, 1.7, 3], "yaw": 0.4, "pitch": 0.2}
        await self.send(aw, pose)
        update = await self.recv(b)
        self.assertEqual(update["id"], welcome_a["id"])
        self.assertEqual(update["position"], [1, 1.7, 3])
        c, cw = await self.connect("Carol")
        welcome_c = await self.recv(c)
        self.assertEqual(len(welcome_c["players"]), 2)
        self.assertEqual(welcome_c["players"][0]["pose"]["seq"], 0)
        self.assertEqual((await self.recv(a))["id"], welcome_c["id"])
        self.assertEqual((await self.recv(b))["id"], welcome_c["id"])
        cw.close()
        self.assertEqual((await self.recv(a))["type"], "left")
        self.assertEqual((await self.recv(b))["type"], "left")
        await self.send(bw, {"type": "ping"})
        self.assertEqual(await self.recv(b), {"type": "pong"})

    async def test_connection_cap_keeps_existing_player_connected(self):
        self.relay.max_peers = 1
        first, writer = await self.connect("Alice")
        await self.recv(first)
        # Excess connections may be closed before or just after sending hello.
        try:
            second, _ = await self.connect("Bob")
            self.assertIsNone(await self.recv(second))
        except (ConnectionError, OSError):
            pass
        await self.send(writer, {"type": "ping"})
        self.assertEqual(await self.recv(first), {"type": "pong"})

    async def test_concurrent_joins_have_no_duplicate_or_missing_players(self):
        connections = await asyncio.gather(*(self.connect(f"Player{i}") for i in range(8)))
        for reader, _ in connections:
            welcome = await self.recv(reader)
            ids = [player["id"] for player in welcome["players"]]
            while len(ids) < 7:
                event = await self.recv(reader)
                self.assertEqual(event["type"], "joined")
                ids.append(event["id"])
            self.assertEqual(len(set(ids)), 7)
            self.assertNotIn(welcome["id"], ids)

    async def test_oversized_frame_disconnects(self):
        reader, writer = await self.connect("Alice")
        await self.recv(reader)
        writer.write(b"x" * (MAX_FRAME + 1) + b"\n")
        await writer.drain()
        self.assertIsNone(await self.recv(reader))

    async def test_auth_failure_does_not_join(self):
        reader, _ = await self.connect("Intruder", "bad-token")
        self.assertIsNone(await self.recv(reader))
        self.assertEqual(self.relay.peers, {})

    async def test_invalid_or_replayed_pose_disconnects(self):
        reader, writer = await self.connect("Alice")
        await self.recv(reader)
        pose = {"type": "pose", "seq": 0, "position": [0, 1.7, 0], "yaw": 0, "pitch": 0}
        await self.send(writer, pose)
        await self.send(writer, pose)
        self.assertIsNone(await self.recv(reader))

    async def test_spoofing_and_out_of_bounds_are_rejected(self):
        for edit in ({"id": "someone-else"}, {"position": [0, 999, 0]}, {"seq": True}):
            reader, writer = await self.connect("Alice")
            await self.recv(reader)
            pose = {"type": "pose", "seq": 0, "position": [0, 1.7, 0], "yaw": 0, "pitch": 0}
            pose.update(edit)
            await self.send(writer, pose)
            self.assertIsNone(await self.recv(reader))

    async def test_bad_certificate_is_rejected(self):
        untrusted = ssl.create_default_context()
        loop = asyncio.get_running_loop()
        previous = loop.get_exception_handler()
        def handle_expected_reset(loop, context):
            if (context.get("message") == "Error on transport creation for incoming connection"
                    and isinstance(context.get("exception"), ConnectionResetError)):
                return  # The client intentionally rejected the server certificate.
            if previous:
                previous(loop, context)
            else:
                loop.default_exception_handler(context)
        loop.set_exception_handler(handle_expected_reset)
        try:
            with self.assertRaises(ssl.SSLCertVerificationError):
                await asyncio.open_connection("127.0.0.1", self.port, ssl=untrusted,
                                              server_hostname="localhost")
            await asyncio.sleep(0.02)  # Allow the server-side TLS reset to be reported.
        finally:
            loop.set_exception_handler(previous)

    async def test_message_rate_is_limited(self):
        reader, writer = await self.connect("Alice")
        await self.recv(reader)
        writer.write(encode({"type": "ping"}) * 31)
        await writer.drain()
        for _ in range(30):
            self.assertEqual(await self.recv(reader), {"type": "pong"})
        self.assertIsNone(await self.recv(reader))


if __name__ == "__main__":
    unittest.main()
