# Experimental online-play foundation

**This is not yet multiplayer sandbox gameplay.** `sandbox_relay.py` is a
standalone, in-memory TLS presence relay and protocol test bed. Experimental
clients render simple remote player markers, but do not synchronize building,
physics, vehicles, interactions or FPV drones. Other players do not collide with
or interact with the local world. Do not use this as a public production server.

The current sandbox simulations run independently in three render loops. Full
online play still needs an authoritative shared simulation, stable object and
player IDs, command validation and reconciliation, snapshots/reconnect,
server persistence, gameplay integration and cross-platform testing. In
particular, the relay trusts client-reported positions and does not check
collisions or prevent cheating. Client integrations need device testing.

## Run for development

Python 3.10+ and a certificate/key in PEM format are required. Use a certificate
trusted by clients for the hostname they connect to. TLS is mandatory; there is
no cleartext mode. Set a randomly generated secret of at least 32 UTF-8 bytes
in `EKRON_JOIN_TOKEN` using your deployment's secret-management facility.

```sh
# Read the provisioned secret without putting it in shell history.
read -rs EKRON_JOIN_TOKEN; export EKRON_JOIN_TOKEN
python3 server/sandbox_relay.py --cert /path/to/cert.pem --key /path/to/key.pem
```

The relay listens on `127.0.0.1:38475` by default. `--host 0.0.0.0` explicitly
allows remote connections and should only be used in a trusted, protected test
network. It is not an invitation to expose this prototype to the internet:
TLS handshake connections are not globally rate-limited, there is no account
system, ban system, protection against token sharing, abuse controls, or
security review. All participants share the same token. Never commit secrets,
keys, or certificates to the repository. The relay does not write world state
or log the token. Restarting it disconnects everyone and loses the session.

## Experimental clients

- Android and iOS: start the sandbox, open the pause menu and select the
  experimental online option. Enter the server hostname, port, player name and
  join token. Use the matching disconnect action to leave. The connection ends
  when the app is backgrounded; connect again after returning.
- Linux/macOS desktop: build with OpenSSL 1.1.1+ development headers and
  libraries installed. CMake reports whether experimental presence is enabled.
  Supply the host and optional port/name on the command line, and the shared
  secret through the environment, not the command line:

  ```sh
  read -rs EKRON_JOIN_TOKEN; export EKRON_JOIN_TOKEN
  ./ekron-realms --online-host sandbox.example.com --online-port 38475 --online-name Player
  ```

  On macOS use the executable inside the application bundle. Connection status
  is displayed in the pause menu. Exit the application to disconnect. Desktop
  builds without OpenSSL still work offline and report online as unavailable.

All clients require certificate and hostname verification; no insecure bypass
is provided. Use a certificate trusted by each platform for the exact hostname
or IP entered (Android normally does not trust user-installed CAs by default).
A LAN connection on iOS can prompt for local-network permission. Distribute the
join token privately: every participant can see and reuse it. Hostnames and the
server operator are trusted; untrusted servers may send malicious data. These
client integrations have not been security-audited. Transport does not provide
account identity, cheating protection, persistence, or world synchronization.

Run the protocol and live TLS tests with:

```sh
python3 -m unittest discover -s tests/network -v
```

The desktop TLS integration tests require compiling the native test with OpenSSL
and setting `EKRON_DESKTOP_PRESENCE_TEST` to its path, as shown in
`.github/workflows/network-foundation.yml`.

For device validation, start a relay on a protected network with a trusted
certificate. Connect two clients (including Android, iOS, and desktop in
separate runs), move each player, and verify that the other sees a marker and
that it disappears when the player disconnects or backgrounds the app. Also
check that a wrong token, untrusted certificate, or mismatched hostname fails.
These device and cross-platform checks have not yet been performed.

Tests require the `openssl` command to create a temporary self-signed
certificate; clients verify that certificate explicitly.

## Prototype protocol, version 1

TLS carries UTF-8 JSON objects terminated by a newline. Client-to-server frames
are at most 4096 bytes including the newline; server-to-client frames are at
most 16384 bytes to allow a full player list. Clients must validate the server's
certificate and host name. A client must send the first frame within five seconds:

```json
{"type":"hello","version":1,"token":"<secret>","name":"Alice"}
```

On success the relay sends `welcome`, with a server-generated 128-bit random
hex `id` and a `players` array of existing `{id,name,pose}` records. `pose` is
null until that player sends an update. Existing participants receive
`{"type":"joined","id":"...","name":"Alice"}`; all remaining participants
receive `{"type":"left","id":"..."}` on disconnect.

A participant may send the following frame, with a strictly increasing integer
sequence number and the player's camera position and orientation in radians:

```json
{"type":"pose","seq":0,"position":[0,1.7,0],"yaw":0,"pitch":0}
```

The relay forwards the validated message to the other participants, adding the
server-assigned `id`. Invalid, oversized, repeated or unsupported frames close
the connection; the server does not accept client-supplied IDs. Bounds are
±512 for X/Z, -100 through 200 for Y, ±1.3 for pitch, and ±1,000,000 for yaw.
Send `{"type":"ping"}` to receive `{"type":"pong"}` during idle periods;
connections time out after 30 seconds. There are at most 16 concurrent
connections (including connections waiting to authenticate), and at most 30
messages per connection per second. Slow clients are disconnected.

This protocol is experimental and may change when the authoritative game
server and real clients are implemented. No compatibility or security guarantee
is made for untrusted environments.
