package com.pixelcyd.companion.store

import android.app.Notification
import android.content.Context
import android.content.SharedPreferences
import com.pixelcyd.companion.protocol.CompanionProtocol.ChatBubble
import com.pixelcyd.companion.protocol.CompanionProtocol.WhatsAppMessage
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import org.json.JSONArray
import org.json.JSONObject
import java.security.SecureRandom
import java.util.concurrent.ConcurrentHashMap

data class CompanionUiState(
    val serverRunning: Boolean = false,
    val localIp: String = "",
    val port: Int = 8765,
    val pairingToken: String = "",
    val connectedDevices: Int = 0,
    val lastDeviceName: String = "",
    val forwardingEnabled: Boolean = true,
    val notificationAccessGranted: Boolean = false,
    val listenerBound: Boolean = false,
    val recentCount: Int = 0,
    val lastEvent: String = "Idle"
)

data class ReplyActionEntry(
    val sbnKey: String,
    val action: Notification.Action?
)

object CompanionRepository {
    private const val PREFS_NAME = "pixel_cyd_companion"
    private const val KEY_TOKEN = "pairing_token"
    private const val KEY_FORWARDING = "forwarding_enabled"
    private const val KEY_HISTORY = "wa_chat_history"
    private const val MAX_HISTORY = 30

    private var appPrefs: android.content.SharedPreferences? = null
    private val lock = Any()
    private val history = ArrayDeque<WhatsAppMessage>(MAX_HISTORY)
    private val activeActions = ConcurrentHashMap<String, ReplyActionEntry>()
    private val dedupFingerprints = ConcurrentHashMap<String, Long>()

    private val _uiState = MutableStateFlow(CompanionUiState())
    val uiState: StateFlow<CompanionUiState> = _uiState.asStateFlow()

    @Volatile
    var onNewNotification: ((WhatsAppMessage) -> Unit)? = null

    @Volatile
    var onStatusChanged: (() -> Unit)? = null

    @Volatile
    var onSendToCyd: ((String) -> Boolean)? = null

    fun sendCommandToCyd(payload: String): Boolean {
        return onSendToCyd?.invoke(payload) == true
    }

    fun init(context: Context) {
        val prefs = context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
        appPrefs = prefs
        var token = prefs.getString(KEY_TOKEN, null)
        if (token.isNullOrBlank()) {
            token = generateToken()
            prefs.edit().putString(KEY_TOKEN, token).apply()
        }
        val forwarding = prefs.getBoolean(KEY_FORWARDING, true)
        loadPersistedHistory(prefs)
        val loadedCount = synchronized(lock) { history.size }
        updateUi { it.copy(pairingToken = token, forwardingEnabled = forwarding, recentCount = loadedCount) }
    }

    fun regenerateToken(context: Context): String {
        val token = generateToken()
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putString(KEY_TOKEN, token)
            .apply()
        updateUi { it.copy(pairingToken = token, lastEvent = "Pairing token regenerated") }
        return token
    }

    fun setForwardingEnabled(context: Context, enabled: Boolean) {
        context.getSharedPreferences(PREFS_NAME, Context.MODE_PRIVATE)
            .edit()
            .putBoolean(KEY_FORWARDING, enabled)
            .apply()
        updateUi {
            it.copy(
                forwardingEnabled = enabled,
                lastEvent = if (enabled) "WhatsApp forwarding enabled" else "WhatsApp forwarding paused"
            )
        }
        onStatusChanged?.invoke()
    }

    fun getPairingToken(): String = _uiState.value.pairingToken
    fun isForwardingEnabled(): Boolean = _uiState.value.forwardingEnabled

    fun updateServerState(running: Boolean, ip: String, port: Int, event: String? = null) {
        updateUi {
            it.copy(
                serverRunning = running,
                localIp = ip,
                port = port,
                lastEvent = event ?: it.lastEvent
            )
        }
    }

    fun updateClientCount(count: Int, deviceName: String? = null, event: String? = null) {
        updateUi {
            it.copy(
                connectedDevices = count,
                lastDeviceName = deviceName ?: it.lastDeviceName,
                lastEvent = event ?: it.lastEvent
            )
        }
    }

    fun updateListenerStatus(accessGranted: Boolean, bound: Boolean) {
        updateUi {
            it.copy(
                notificationAccessGranted = accessGranted,
                listenerBound = bound
            )
        }
        onStatusChanged?.invoke()
    }

    fun recordNotification(msg: WhatsAppMessage, replyAction: Notification.Action?): Boolean {
        // Deduplicate identical sender + text updates within 10 seconds
        val fp = "${msg.id}|${msg.sender}|${msg.text}"
        val now = System.currentTimeMillis()
        val prev = dedupFingerprints[fp]
        if (prev != null && (now - prev) < 10_000L) {
            // Still refresh the active reply action handle in case PendingIntent rotated
            activeActions[msg.id] = ReplyActionEntry(msg.sbnKey, replyAction)
            return false
        }
        dedupFingerprints[fp] = now
        if (dedupFingerprints.size > 120) {
            dedupFingerprints.entries.removeIf { (now - it.value) > 60_000L }
        }

        activeActions[msg.id] = ReplyActionEntry(msg.sbnKey, replyAction)

        synchronized(lock) {
            val existing = history.firstOrNull { it.id == msg.id }
            val merged = if (msg.avatarRgb565B64 == null && existing?.avatarRgb565B64 != null) {
                msg.copy(avatarRgb565B64 = existing.avatarRgb565B64)
            } else {
                msg
            }
            history.removeAll { it.id == msg.id }
            history.addFirst(merged)
            while (history.size > MAX_HISTORY) {
                history.removeLast()
            }
            persistHistoryLocked()
        }

        updateUi {
            it.copy(
                recentCount = synchronized(lock) { history.size },
                lastEvent = "Forwarded message from ${msg.sender}"
            )
        }

        onNewNotification?.invoke(msg)
        return true
    }

    fun markNotificationRemoved(sbnKey: String) {
        activeActions.entries.removeIf { it.value.sbnKey == sbnKey }
    }

    fun getReplyAction(notifId: String): ReplyActionEntry? = activeActions[notifId]

    fun getRecentHistory(limit: Int): List<WhatsAppMessage> = synchronized(lock) {
        history.take(limit.coerceIn(1, MAX_HISTORY))
    }

    fun removeFromHistory(notifId: String) {
        activeActions.remove(notifId)
        synchronized(lock) {
            history.removeAll { it.id == notifId }
            persistHistoryLocked()
        }
        updateUi { it.copy(recentCount = synchronized(lock) { history.size }) }
    }

    fun clearHistory() {
        synchronized(lock) {
            history.clear()
            persistHistoryLocked()
        }
        updateUi { it.copy(recentCount = 0, lastEvent = "Cleared local history") }
    }

    private fun persistHistoryLocked() {
        val prefs = appPrefs ?: return
        try {
            val arr = JSONArray()
            for (item in history) {
                val obj = item.toHistoryItemJson()
                obj.put("sbnKey", item.sbnKey)
                if (!item.avatarRgb565B64.isNullOrBlank()) {
                    obj.put("avatar", item.avatarRgb565B64)
                }
                arr.put(obj)
            }
            prefs.edit().putString(KEY_HISTORY, arr.toString()).apply()
        } catch (_: Exception) {
        }
    }

    private fun loadPersistedHistory(prefs: SharedPreferences) {
        val raw = prefs.getString(KEY_HISTORY, null) ?: return
        try {
            val arr = JSONArray(raw)
            synchronized(lock) {
                history.clear()
                for (i in 0 until arr.length()) {
                    val obj = arr.optJSONObject(i) ?: continue
                    val id = obj.optString("id", "")
                    if (id.isBlank()) continue
                    val bubbles = ArrayList<ChatBubble>()
                    val bArr = obj.optJSONArray("messages")
                    if (bArr != null) {
                        for (j in 0 until bArr.length()) {
                            val bo = bArr.optJSONObject(j) ?: continue
                            bubbles.add(
                                ChatBubble(
                                    sender = bo.optString("sender", ""),
                                    text = bo.optString("text", ""),
                                    time = bo.optString("time", ""),
                                    outgoing = bo.optBoolean("outgoing", false)
                                )
                            )
                        }
                    }
                    val av = obj.optString("avatar", "").ifBlank { null }
                    history.addLast(
                        WhatsAppMessage(
                            id = id,
                            sbnKey = obj.optString("sbnKey", ""),
                            sender = obj.optString("sender", "WhatsApp"),
                            conversation = obj.optString("conversation", "WhatsApp"),
                            text = obj.optString("text", ""),
                            timestampMs = obj.optLong("timestamp", 0L),
                            canReply = false,
                            messages = bubbles,
                            avatarRgb565B64 = av
                        )
                    )
                    if (history.size >= MAX_HISTORY) break
                }
            }
        } catch (_: Exception) {
        }
    }

    private fun updateUi(transform: (CompanionUiState) -> CompanionUiState) {
        _uiState.value = transform(_uiState.value)
    }

    private fun generateToken(): String {
        val rng = SecureRandom()
        val num = rng.nextInt(900000) + 100000
        return num.toString()
    }
}
