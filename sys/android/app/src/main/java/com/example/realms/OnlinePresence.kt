package com.example.realms

import android.os.Handler
import android.os.Looper
import androidx.annotation.StringRes
import org.json.JSONArray
import org.json.JSONObject
import java.io.ByteArrayOutputStream
import java.net.InetSocketAddress
import java.net.Socket
import java.net.SocketTimeoutException
import javax.net.ssl.SSLSocket
import javax.net.ssl.SSLSocketFactory
import kotlin.math.abs

/** Experimental TLS presence client. It does not synchronize sandbox objects.
 *  Status messages are localized string resources (values/, values-ru/, values-pt-rPT/). */
class OnlinePresence(
    private val onPlayers: (FloatArray) -> Unit,
    private val onStatus: (Int) -> Unit
) {
    private val main = Handler(Looper.getMainLooper())
    private var generation = 0L
    private var socket: Socket? = null
    @Volatile private var localPose: FloatArray? = null

    @Synchronized private fun current(id: Long) = id == generation
    @Synchronized private fun install(id: Long, next: Socket): Boolean {
        if (id != generation) { next.close(); return false }
        socket = next
        return true
    }
    private fun status(id: Long, @StringRes text: Int) = main.post { if (current(id)) onStatus(text) }
    private fun players(id: Long, values: FloatArray) = main.post { if (current(id)) onPlayers(values) }

    fun updatePose(values: FloatArray) {
        if (values.size >= 17 && values[4].toInt() == 6)
            localPose = floatArrayOf(values[0], values[1], values[2], values[15], values[16])
    }

    @Synchronized fun disconnect() {
        generation++
        try { socket?.close() } catch (_: Exception) {}
        socket = null
        localPose = null
        onPlayers(floatArrayOf())
    }

    fun connect(host: String, port: Int, name: String, token: String) {
        disconnect()
        val id = synchronized(this) { generation }
        status(id, R.string.online_status_connecting)
        Thread({ runClient(id, host, port, name, token) }, "EkronPresence").apply {
            isDaemon = true
            start()
        }
    }

    private fun runClient(id: Long, host: String, port: Int, name: String, token: String) {
        val peers = linkedMapOf<String, FloatArray?>()
        val sequences = mutableMapOf<String, Long>()
        var welcomed = false
        try {
            val raw = Socket()
            if (!install(id, raw)) return
            raw.connect(InetSocketAddress(host, port), 5000)
            raw.soTimeout = 5000
            val tls = (SSLSocketFactory.getDefault() as SSLSocketFactory)
                .createSocket(raw, host, port, true) as SSLSocket
            if (!install(id, tls)) return
            val parameters = tls.sslParameters
            parameters.endpointIdentificationAlgorithm = "HTTPS"
            tls.sslParameters = parameters
            tls.startHandshake() // System trust and hostname verification are mandatory.
            tls.soTimeout = 150
            val input = tls.inputStream
            val output = tls.outputStream
            fun send(value: JSONObject) {
                val frame = (value.toString() + "\n").toByteArray(Charsets.UTF_8)
                require(frame.size <= 4096)
                output.write(frame)
                output.flush()
            }
            send(JSONObject().put("type", "hello").put("version", 1).put("token", token).put("name", name))
            var seq = 0L
            var lastSend = 0L
            val helloDeadline = android.os.SystemClock.elapsedRealtime() + 5000
            val frame = ByteArrayOutputStream()
            val bytes = ByteArray(2048)
            fun validId(value: String) = value.matches(Regex("[0-9a-f]{32}"))
            fun decodePose(obj: JSONObject): FloatArray {
                val position = obj.getJSONArray("position")
                require(position.length() == 3)
                val v = floatArrayOf(position.getDouble(0).toFloat(), position.getDouble(1).toFloat(),
                    position.getDouble(2).toFloat(), obj.getDouble("yaw").toFloat())
                require(v.all { it.isFinite() } && abs(v[0]) <= 512 && v[1] in -100f..200f &&
                    abs(v[2]) <= 512 && abs(v[3]) <= 1000000)
                val pitch = obj.getDouble("pitch")
                require(pitch.isFinite() && abs(pitch) <= 1.3)
                return v
            }
            fun publish() {
                val flat = peers.values.filterNotNull().flatMap { it.asList() }.toFloatArray()
                players(id, flat)
            }
            fun receive(line: ByteArray) {
                var depth = 0
                var quoted = false
                var escaped = false
                for (byte in line) {
                    val ch = byte.toInt().toChar()
                    if (quoted) {
                        if (escaped) escaped = false else if (ch == '\\') escaped = true else if (ch == '"') quoted = false
                    } else if (ch == '"') quoted = true
                    else if (ch == '{' || ch == '[') { depth++; require(depth <= 8) }
                    else if (ch == '}' || ch == ']') { depth--; require(depth >= 0) }
                }
                require(!quoted && depth == 0)
                val msg = JSONObject(line.toString(Charsets.UTF_8))
                when (msg.getString("type")) {
                    "welcome" -> {
                        require(!welcomed && msg.getInt("version") == 1 && validId(msg.getString("id")))
                        val list = msg.getJSONArray("players")
                        require(list.length() <= 15)
                        for (i in 0 until list.length()) {
                            val player = list.getJSONObject(i)
                            val key = player.getString("id")
                            require(validId(key) && !peers.containsKey(key))
                            val pose = player.optJSONObject("pose")
                            peers[key] = pose?.let { decodePose(it) }
                            if (pose != null) sequences[key] = pose.getLong("seq")
                        }
                        welcomed = true
                        publish()
                        status(id, R.string.online_status_connected)
                    }
                    "joined" -> {
                        require(welcomed)
                        val key = msg.getString("id")
                        require(validId(key) && !peers.containsKey(key) && peers.size < 15)
                        peers[key] = null
                        publish()
                    }
                    "pose" -> {
                        require(welcomed)
                        val key = msg.getString("id")
                        val sequence = msg.getLong("seq")
                        require(peers.containsKey(key) && sequence >= 0 && sequence > (sequences[key] ?: -1L))
                        peers[key] = decodePose(msg)
                        sequences[key] = sequence
                        publish()
                    }
                    "left" -> {
                        require(welcomed)
                        val key = msg.getString("id")
                        require(peers.containsKey(key))
                        peers.remove(key); sequences.remove(key); publish()
                    }
                    "pong" -> require(welcomed)
                    else -> throw IllegalArgumentException("invalid server message")
                }
            }
            while (current(id)) {
                val now = android.os.SystemClock.elapsedRealtime()
                if (!welcomed && now >= helloDeadline) throw SocketTimeoutException("welcome timeout")
                if (welcomed && now - lastSend >= 100) {
                    val pose = localPose
                    if (pose != null && pose.all { it.isFinite() } && abs(pose[0]) <= 512 &&
                        pose[1] in -100f..200f && abs(pose[2]) <= 512 && abs(pose[3]) <= 1000000 &&
                        abs(pose[4].toDouble()) <= 1.3) {
                        send(JSONObject().put("type", "pose").put("seq", seq++)
                            .put("position", JSONArray().put(pose[0].toDouble()).put(pose[1].toDouble()).put(pose[2].toDouble()))
                            .put("yaw", pose[3].toDouble()).put("pitch", pose[4].toDouble()))
                    } else {
                        send(JSONObject().put("type", "ping"))
                    }
                    lastSend = now
                }
                try {
                    val count = input.read(bytes)
                    if (count < 0) break
                    for (i in 0 until count) {
                        if (bytes[i] == '\n'.code.toByte()) {
                            receive(frame.toByteArray())
                            frame.reset()
                        } else {
                            require(frame.size() < 16383)
                            frame.write(bytes[i].toInt())
                        }
                    }
                } catch (_: SocketTimeoutException) { /* Send poses even when no one else is moving. */ }
            }
        } catch (_: Exception) {
            if (current(id)) status(id, R.string.online_status_failed)
        } finally {
            synchronized(this) {
                if (id == generation) {
                    try { socket?.close() } catch (_: Exception) {}
                    socket = null
                    players(id, floatArrayOf())
                    if (welcomed) status(id, R.string.online_status_closed)
                }
            }
        }
    }
}
