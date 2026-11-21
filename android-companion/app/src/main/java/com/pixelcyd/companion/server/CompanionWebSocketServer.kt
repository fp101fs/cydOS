package com.pixelcyd.companion.server

import android.content.Context
import com.pixelcyd.companion.notifications.WhatsAppNotificationListener
import com.pixelcyd.companion.protocol.CompanionProtocol
import com.pixelcyd.companion.store.CompanionRepository
import org.java_websocket.WebSocket
import org.java_websocket.handshake.ClientHandshake
import org.java_websocket.server.WebSocketServer
import org.json.JSONObject
import java.net.InetSocketAddress
import java.util.concurrent.ConcurrentHashMap

class CompanionWebSocketServer(
    private val appContext: Context,
    port: Int
) : WebSocketServer(InetSocketAddress(port)) {

    private val authenticatedClients = ConcurrentHashMap<WebSocket, String>()

    init {
        isReuseAddr = true
        connectionLostTimeout = 30
    }

    override fun onStart() {
        CompanionRepository.updateClientCount(0, event = "Server listening on port $port")
    }

    override fun onOpen(conn: WebSocket, handshake: ClientHandshake) {
        // Client must authenticate with a valid `hello` + pairing token before receiving data
    }

    override fun onClose(conn: WebSocket, code: Int, reason: String?, remote: Boolean) {
        val dev = authenticatedClients.remove(conn)
        if (dev != null) {
            CompanionRepository.updateClientCount(
                authenticatedClients.size,
                event = "$dev disconnected"
            )
        }
    }

    override fun onError(conn: WebSocket?, ex: Exception?) {
        if (conn != null) {
            authenticatedClients.remove(conn)
            CompanionRepository.updateClientCount(authenticatedClients.size)
        }
    }

    override fun onMessage(conn: WebSocket, message: String) {
        if (message.toByteArray().size > CompanionProtocol.MAX_INBOUND_BYTES) {
            conn.close(1009, "Payload too large")
            return
        }

        val doc = try {
            JSONObject(message)
        } catch (_: Exception) {
            return
        }

        val type = doc.optString("type", "")
        if (type.isBlank()) return

        if (type == "hello") {
            handleHello(conn, doc)
            return
        }

        // Reject any command before pairing token authentication
        if (!authenticatedClients.containsKey(conn)) {
            conn.send(
                CompanionProtocol.helloAck(
                    ok = false,
                    forwardingEnabled = CompanionRepository.isForwardingEnabled(),
                    error = "Not authenticated"
                )
            )
            conn.close(1008, "Unauthorized")
            return
        }

        when (type) {
            "ping" -> conn.send(CompanionProtocol.pong())
            "history_request" -> {
                val limit = doc.optInt("limit", 12).coerceIn(1, 30)
                val items = CompanionRepository.getRecentHistory(limit)
                conn.send(CompanionProtocol.historyResponse(items))
                for (item in items) {
                    val av = item.avatarRgb565B64
                    if (!av.isNullOrBlank()) {
                        try {
                            conn.send(CompanionProtocol.avatarFrame(item.id, av))
                        } catch (_: Exception) {
                        }
                    }
                }
            }
            "reply_request" -> handleReplyRequest(conn, doc)
            "dismiss_request" -> handleDismissRequest(conn, doc)
        }
    }

    private fun handleHello(conn: WebSocket, doc: JSONObject) {
        val expectedToken = CompanionRepository.getPairingToken()
        val providedToken = doc.optString("token", "").trim()
        val deviceName = doc.optString("device", "Pixel-CYD").take(32)

        if (expectedToken.isNotBlank() && providedToken != expectedToken) {
            conn.send(
                CompanionProtocol.helloAck(
                    ok = false,
                    forwardingEnabled = CompanionRepository.isForwardingEnabled(),
                    error = "Invalid pairing token"
                )
            )
            conn.close(1008, "Invalid token")
            CompanionRepository.updateClientCount(
                authenticatedClients.size,
                event = "Rejected invalid pairing token"
            )
            return
        }

        authenticatedClients[conn] = deviceName
        conn.send(
            CompanionProtocol.helloAck(
                ok = true,
                forwardingEnabled = CompanionRepository.isForwardingEnabled()
            )
        )
        CompanionRepository.updateClientCount(
            authenticatedClients.size,
            deviceName = deviceName,
            event = "$deviceName connected"
        )
    }

    private fun handleReplyRequest(conn: WebSocket, doc: JSONObject) {
        val reqId = doc.optLong("req_id", 0L)
        val notifId = doc.optString("id", "").trim()
        val replyText = doc.optString("text", "").trim()

        if (notifId.isBlank() || replyText.isBlank()) {
            conn.send(CompanionProtocol.replyResult(reqId, "failed", "Empty reply"))
            return
        }
        if (replyText.length > CompanionProtocol.MAX_REPLY_CHARS) {
            conn.send(CompanionProtocol.replyResult(reqId, "failed", "Reply too long"))
            return
        }

        val (status, detail) = WhatsAppNotificationListener.sendReply(appContext, notifId, replyText)
        conn.send(CompanionProtocol.replyResult(reqId, status, detail))
    }

    private fun handleDismissRequest(conn: WebSocket, doc: JSONObject) {
        val notifId = doc.optString("id", "").trim()
        if (notifId.isBlank()) {
            conn.send(CompanionProtocol.dismissResult("", false, "Missing id"))
            return
        }
        val (ok, detail) = WhatsAppNotificationListener.dismissNotification(notifId)
        conn.send(CompanionProtocol.dismissResult(notifId, ok, detail))
    }

    fun broadcastNotification(msg: CompanionProtocol.WhatsAppMessage) {
        val payload = msg.toNotificationJson().toString()
        broadcastToAuthenticated(payload)
        val av = msg.avatarRgb565B64
        if (!av.isNullOrBlank()) {
            broadcastToAuthenticated(CompanionProtocol.avatarFrame(msg.id, av))
        }
    }

    fun broadcastStatus() {
        val ui = CompanionRepository.uiState.value
        val payload = CompanionProtocol.status(
            forwardingEnabled = ui.forwardingEnabled,
            listenerConnected = ui.listenerBound
        )
        broadcastToAuthenticated(payload)
    }

    private fun broadcastToAuthenticated(payload: String) {
        for (client in authenticatedClients.keys) {
            if (client.isOpen) {
                try {
                    client.send(payload)
                } catch (_: Exception) {
                }
            }
        }
    }
}
