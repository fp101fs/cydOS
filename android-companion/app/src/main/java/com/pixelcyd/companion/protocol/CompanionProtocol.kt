package com.pixelcyd.companion.protocol

import org.json.JSONArray
import org.json.JSONObject
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

object CompanionProtocol {
    const val PROTOCOL_VERSION = 1
    const val DEFAULT_PORT = 8765
    const val MAX_INBOUND_BYTES = 8192
    const val MAX_REPLY_CHARS = 512
    const val AVATAR_SIZE = 24

    data class ChatBubble(
        val sender: String,
        val text: String,
        val time: String,
        val outgoing: Boolean = false
    ) {
        fun toJson(): JSONObject = JSONObject().apply {
            put("sender", sender)
            put("text", text)
            put("time", time)
            put("outgoing", outgoing)
        }
    }

    data class WhatsAppMessage(
        val id: String,
        val sbnKey: String,
        val sender: String,
        val conversation: String,
        val text: String,
        val timestampMs: Long,
        val canReply: Boolean,
        val messages: List<ChatBubble> = emptyList(),
        val avatarRgb565B64: String? = null
    ) {
        fun formattedTime(): String {
            val fmt = SimpleDateFormat("HH:mm", Locale.getDefault())
            return fmt.format(Date(timestampMs))
        }

        fun toNotificationJson(): JSONObject = JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "notification")
            put("id", id)
            put("sender", sender)
            put("conversation", conversation)
            put("text", text)
            put("timestamp", timestampMs)
            put("time", formattedTime())
            put("can_reply", canReply)
            if (messages.isNotEmpty()) {
                val arr = JSONArray()
                for (m in messages) arr.put(m.toJson())
                put("messages", arr)
            }
        }

        fun toHistoryItemJson(): JSONObject = JSONObject().apply {
            put("id", id)
            put("sender", sender)
            put("conversation", conversation)
            put("text", text)
            put("timestamp", timestampMs)
            put("time", formattedTime())
            put("can_reply", canReply)
            if (messages.isNotEmpty()) {
                val arr = JSONArray()
                for (m in messages) arr.put(m.toJson())
                put("messages", arr)
            }
        }
    }

    fun avatarFrame(id: String, rgb565B64: String): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "avatar")
            put("id", id)
            put("w", AVATAR_SIZE)
            put("h", AVATAR_SIZE)
            put("rgb565", rgb565B64)
        }.toString()

    fun helloAck(ok: Boolean, forwardingEnabled: Boolean, error: String? = null): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "hello_ack")
            put("ok", ok)
            put("forwarding_enabled", forwardingEnabled)
            if (!error.isNullOrBlank()) put("error", error)
        }.toString()

    fun status(forwardingEnabled: Boolean, listenerConnected: Boolean): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "status")
            put("forwarding_enabled", forwardingEnabled)
            put("listener_connected", listenerConnected)
        }.toString()

    fun historyResponse(items: List<WhatsAppMessage>): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "history_response")
            val arr = JSONArray()
            for (item in items) {
                arr.put(item.toHistoryItemJson())
            }
            put("items", arr)
        }.toString()

    fun replyResult(reqId: Long, status: String, detail: String = ""): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "reply_result")
            put("req_id", reqId)
            put("status", status) // "sent" | "failed" | "unsupported"
            if (detail.isNotBlank()) put("detail", detail)
        }.toString()

    fun dismissResult(id: String, ok: Boolean, detail: String = ""): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "dismiss_result")
            put("id", id)
            put("ok", ok)
            if (detail.isNotBlank()) put("detail", detail)
        }.toString()

    fun pong(): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "pong")
        }.toString()
}
