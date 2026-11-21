package com.pixelcyd.companion.notifications

import android.app.Notification
import android.app.RemoteInput
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.os.Bundle
import android.provider.Settings
import android.service.notification.NotificationListenerService
import android.service.notification.StatusBarNotification
import com.pixelcyd.companion.protocol.CompanionProtocol.WhatsAppMessage
import com.pixelcyd.companion.store.CompanionRepository
import java.security.MessageDigest

class WhatsAppNotificationListener : NotificationListenerService() {

    companion object {
        private val WHATSAPP_PACKAGES = setOf(
            "com.whatsapp",
            "com.whatsapp.w4b"
        )

        @Volatile
        private var instance: WhatsAppNotificationListener? = null

        fun isNotificationAccessGranted(context: Context): Boolean {
            val enabled = Settings.Secure.getString(
                context.contentResolver,
                "enabled_notification_listeners"
            ) ?: return false
            val target = ComponentName(context, WhatsAppNotificationListener::class.java).flattenToString()
            return enabled.split(':').any { it.equals(target, ignoreCase = true) }
        }

        fun sendReply(context: Context, notifId: String, replyText: String): Pair<String, String> {
            val entry = CompanionRepository.getReplyAction(notifId)
                ?: return "unsupported" to "Notification no longer active"

            val action = entry.action
                ?: return "unsupported" to "No reply action on notification"

            val remoteInputs = action.remoteInputs
            if (remoteInputs.isNullOrEmpty()) {
                return "unsupported" to "Notification has no RemoteInput"
            }

            val pendingIntent = action.actionIntent
                ?: return "unsupported" to "Reply PendingIntent missing"

            return try {
                val fillIn = Intent()
                val results = Bundle()
                for (ri in remoteInputs) {
                    results.putCharSequence(ri.resultKey, replyText)
                }
                RemoteInput.addResultsToIntent(remoteInputs, fillIn, results)
                pendingIntent.send(context, 0, fillIn)
                "sent" to ""
            } catch (e: Exception) {
                "failed" to (e.message?.take(48) ?: "Reply send failed")
            }
        }

        fun dismissNotification(notifId: String): Pair<Boolean, String> {
            val svc = instance ?: return false to "Notification listener not active"
            val entry = CompanionRepository.getReplyAction(notifId)
                ?: return false to "Notification already dismissed"
            return try {
                svc.cancelNotification(entry.sbnKey)
                CompanionRepository.removeFromHistory(notifId)
                true to ""
            } catch (e: Exception) {
                false to (e.message?.take(48) ?: "Dismiss failed")
            }
        }
    }

    override fun onListenerConnected() {
        super.onListenerConnected()
        instance = this
        CompanionRepository.updateListenerStatus(
            accessGranted = isNotificationAccessGranted(this),
            bound = true
        )
    }

    override fun onListenerDisconnected() {
        super.onListenerDisconnected()
        instance = null
        CompanionRepository.updateListenerStatus(
            accessGranted = isNotificationAccessGranted(this),
            bound = false
        )
    }

    override fun onNotificationPosted(sbn: StatusBarNotification?) {
        if (sbn == null) return
        if (sbn.packageName !in WHATSAPP_PACKAGES) return
        if (!CompanionRepository.isForwardingEnabled()) return

        val notif = sbn.notification ?: return

        // Skip group summary headers ("3 messages from 2 chats") to avoid duplicates
        if ((notif.flags and Notification.FLAG_GROUP_SUMMARY) != 0) return
        // Skip ongoing call/backup notifications
        if ((notif.flags and Notification.FLAG_ONGOING_EVENT) != 0) return

        val extras = notif.extras ?: return
        val convTitle = extras.getCharSequence(Notification.EXTRA_CONVERSATION_TITLE)?.toString()?.trim().orEmpty()
        val rawTitle = extras.getCharSequence(Notification.EXTRA_TITLE)?.toString()?.trim().orEmpty()
        val rawText = extractLatestMessageText(extras)

        if (rawTitle.isBlank() && rawText.isBlank()) return

        val sender = rawTitle.ifBlank { convTitle.ifBlank { "WhatsApp" } }
        val conversation = convTitle.ifBlank { sender }
        val replyAction = findReplyAction(notif)
        val stableId = stableIdForKey(sbn.key)

        val msg = WhatsAppMessage(
            id = stableId,
            sbnKey = sbn.key,
            sender = sender.take(44),
            conversation = conversation.take(44),
            text = rawText.take(220),
            timestampMs = sbn.postTime,
            canReply = replyAction != null
        )

        // Note: Intentionally do NOT log message body to Logcat for privacy.
        CompanionRepository.recordNotification(msg, replyAction)
    }

    override fun onNotificationRemoved(sbn: StatusBarNotification?) {
        if (sbn == null) return
        if (sbn.packageName !in WHATSAPP_PACKAGES) return
        CompanionRepository.markNotificationRemoved(sbn.key)
    }

    private fun extractLatestMessageText(extras: Bundle): String {
        // Prefer MessagingStyle EXTRA_MESSAGES if present to get the latest message cleanly
        @Suppress("DEPRECATION")
        val parcelables = extras.getParcelableArray(Notification.EXTRA_MESSAGES)
        if (!parcelables.isNullOrEmpty()) {
            val last = parcelables.lastOrNull() as? Bundle
            val text = last?.getCharSequence("text")?.toString()?.trim()
            if (!text.isNullOrBlank()) return text
        }
        val bigText = extras.getCharSequence(Notification.EXTRA_BIG_TEXT)?.toString()?.trim()
        if (!bigText.isNullOrBlank()) return bigText
        return extras.getCharSequence(Notification.EXTRA_TEXT)?.toString()?.trim().orEmpty()
    }

    private fun findReplyAction(notif: Notification): Notification.Action? {
        val actions = notif.actions ?: return null
        return actions.firstOrNull { action ->
            val inputs = action.remoteInputs
            !inputs.isNullOrEmpty() && action.actionIntent != null
        }
    }

    private fun stableIdForKey(key: String): String {
        val digest = MessageDigest.getInstance("SHA-256").digest(key.toByteArray())
        return digest.take(10).joinToString("") { "%02x".format(it) }
    }
}
