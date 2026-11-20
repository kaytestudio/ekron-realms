import asyncio
import os
import pathlib
import ssl
import subprocess
import tempfile
import unittest

from server.sandbox_relay import MAX_FRAME, PresenceRelay

BINARY = os.environ.get("EKRON_DESKTOP_PRESENCE_TEST")
TOKEN = "correct horse battery staple 0123456789"


@unittest.skipUnless(BINARY, "set EKRON_DESKTOP_PRESENCE_TEST to the compiled desktop test")
class DesktopPresenceTests(unittest.IsolatedAsyncioTestCase):
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
        self.context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
        self.context.load_cert_chain(self.cert, self.key)
        self.server = await asyncio.start_server(self.relay.handle, "127.0.0.1", 0,
                                                 ssl=self.context, limit=MAX_FRAME)
        self.port = self.server.sockets[0].getsockname()[1]

    async def asyncTearDown(self):
        self.server.close()
        await self.server.wait_closed()
        for _ in range(100):
            if not self.relay.connections:
                break
            await asyncio.sleep(0.01)
        self.assertEqual(self.relay.connections, 0)

    async def run_client(self, host, token, mode, trusted=True):
        env = os.environ.copy()
        env["SSL_CERT_FILE"] = str(self.cert) if trusted else "/nonexistent-cert.pem"
        env["SSL_CERT_DIR"] = self.tmp.name  # Do not rely on machine-specific trust roots.
        process = await asyncio.create_subprocess_exec(BINARY, host, str(self.port), token, mode,
                    env=env, stdout=asyncio.subprocess.PIPE, stderr=asyncio.subprocess.PIPE)
        stdout, stderr = await asyncio.wait_for(process.communicate(), timeout=12)
        self.assertEqual(process.returncode, 0, (stdout + stderr).decode(errors="replace"))

    async def test_two_native_clients_exchange_poses_and_disconnect(self):
        await self.run_client("localhost", TOKEN, "accept")

    async def test_malicious_server_frames_rejected(self):
        for payload in (b'{"type":"welcome","payload":' + b'[' * 100 + b'0' + b']' * 100 + b'}\n',
                        b'x' * 16385 + b'\n'):
            async def malicious(reader, writer):
                try:
                    await reader.readline()  # A verified TLS client has sent hello.
                    writer.write(payload)
                    await writer.drain()
                    await reader.read()
                except (ConnectionError, OSError):
                    pass  # The client is expected to reject and close.
                finally:
                    writer.close()
                    try:
                        await writer.wait_closed()
                    except (ConnectionError, OSError):
                        pass
            server = await asyncio.start_server(malicious, "127.0.0.1", 0, ssl=self.context)
            original_port = self.port
            self.port = server.sockets[0].getsockname()[1]
            try:
                await self.run_client("localhost", TOKEN, "reject")
            finally:
                self.port = original_port
                server.close()
                await server.wait_closed()

    async def test_certificate_and_hostname_and_token_rejected(self):
        loop = asyncio.get_running_loop()
        previous = loop.get_exception_handler()
        def expected_tls_alert(loop, context):
            if context.get("message") == "Error on transport creation for incoming connection" and isinstance(context.get("exception"), (ssl.SSLError, ConnectionResetError)):
                return
            if previous:
                previous(loop, context)
            else:
                loop.default_exception_handler(context)
        loop.set_exception_handler(expected_tls_alert)
        try:
            await self.run_client("localhost", TOKEN, "reject", trusted=False)
            await self.run_client("127.0.0.1", TOKEN, "reject")
            await self.run_client("localhost", "x" * 32, "reject")
        finally:
            loop.set_exception_handler(previous)
