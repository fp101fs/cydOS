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

    // ========================================================================
    // Screen Mirror & Programmatic Remote Control API
    // ========================================================================

    data class ScreenElement(
        val id: Int,
        val type: String,
        val label: String,
        val x: Int,
        val y: Int,
        val w: Int,
        val h: Int,
        val cx: Int,
        val cy: Int,
        val checked: Boolean = false
    )

    data class CydUiTree(
        val width: Int = 240,
        val height: Int = 320,
        val activeApp: String = "home",
        val activeName: String = "Home",
        val streaming: Boolean = false,
        val remoteEnabled: Boolean = true,
        val keyboardOpen: Boolean = false,
        val quickSettingsOpen: Boolean = false,
        val elements: List<ScreenElement> = emptyList()
    )

    fun parseUiTree(doc: JSONObject): CydUiTree {
        val elems = ArrayList<ScreenElement>()
        val arr = doc.optJSONArray("elements")
        if (arr != null) {
            for (i in 0 until arr.length()) {
                val o = arr.optJSONObject(i) ?: continue
                val x = o.optInt("x", 0)
                val y = o.optInt("y", 0)
                val w = o.optInt("w", 0)
                val h = o.optInt("h", 0)
                elems.add(
                    ScreenElement(
                        id = o.optInt("id", i),
                        type = o.optString("type", "btn"),
                        label = o.optString("label", ""),
                        x = x,
                        y = y,
                        w = w,
                        h = h,
                        cx = o.optInt("cx", x + w / 2),
                        cy = o.optInt("cy", y + h / 2),
                        checked = o.optBoolean("checked", false)
                    )
                )
            }
        }
        return CydUiTree(
            width = doc.optInt("w", 240),
            height = doc.optInt("h", 320),
            activeApp = doc.optString("active_app", "home"),
            activeName = doc.optString("active_name", "Home"),
            streaming = doc.optBoolean("streaming", false),
            remoteEnabled = doc.optBoolean("remote_enabled", true),
            keyboardOpen = doc.optBoolean("keyboard_open", false),
            quickSettingsOpen = doc.optBoolean("quick_settings_open", false),
            elements = elems
        )
    }

    fun mirrorControl(enable: Boolean): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "mirror_control")
            put("enable", enable)
        }.toString()

    fun uiTreeRequest(): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "ui_tree_request")
        }.toString()

    fun remoteInputTap(x: Int, y: Int): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "remote_input")
            put("action", "tap")
            put("x", x.coerceIn(0, 239))
            put("y", y.coerceIn(0, 319))
        }.toString()

    fun remoteInputPointer(action: String, x: Int, y: Int): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "remote_input")
            put("action", action)
            put("x", x.coerceIn(0, 239))
            put("y", y.coerceIn(0, 319))
        }.toString()

    fun remoteInputSwipe(x1: Int, y1: Int, x2: Int, y2: Int, durationMs: Int = 220): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "remote_input")
            put("action", "swipe")
            put("x1", x1.coerceIn(0, 239))
            put("y1", y1.coerceIn(0, 319))
            put("x2", x2.coerceIn(0, 239))
            put("y2", y2.coerceIn(0, 319))
            put("duration_ms", durationMs)
        }.toString()

    fun remoteCmd(
        cmd: String,
        argKey: String? = null,
        argStr: String? = null,
        argInt: Int? = null
    ): String =
        JSONObject().apply {
            put("v", PROTOCOL_VERSION)
            put("type", "remote_cmd")
            put("cmd", cmd)
            if (!argKey.isNullOrBlank() && argStr != null) {
                put(argKey, argStr)
            }
            if (argInt != null) {
                put("value", argInt)
            }
        }.toString()
}
